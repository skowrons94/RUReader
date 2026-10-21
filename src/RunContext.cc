#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include <TDirectory.h>
#include <TGraph.h>
#include <TNamed.h>
#include <TParameter.h>
#include <TAxis.h>

#include "RunContext.h"

namespace {

  const char* kCurrentFile  = "current.txt";
  const char* kStatsFile    = "stats.csv";
  const char* kMetadataFile = "metadata.json";

  const char* kTimeTitle    = "Time since run start [s]";

  std::string Trim( const std::string& text, const char* chars = " \t\r\n" )
  {
    const size_t first = text.find_first_not_of( chars );
    if( first == std::string::npos ) return "";
    const size_t last = text.find_last_not_of( chars );
    return text.substr( first, last - first + 1 );
  }

  bool StartsWith( const std::string& text, const char* prefix )
  {
    return text.compare( 0, std::strlen( prefix ), prefix ) == 0;
  }

  // The whole token has to be a number, otherwise the line is one that was
  // caught half written and is dropped, as WebDAQ does.
  bool ParseNumber( const std::string& token, double& value )
  {
    if( token.empty( ) ) return false;
    char* end = nullptr;
    errno = 0;
    value = std::strtod( token.c_str( ), &end );
    return end == token.c_str( ) + token.size( ) && errno != ERANGE;
  }

  // One CSV record. Fields that contain a comma are quoted by the writer, and a
  // quote inside a quoted field is doubled.
  std::vector<std::string> SplitCsv( const std::string& line )
  {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;

    for( size_t i = 0; i < line.size( ); ++i ){
      const char c = line[i];
      if( quoted ){
        if( c == '"' && i + 1 < line.size( ) && line[i+1] == '"' ){ field += '"'; ++i; }
        else if( c == '"' ) quoted = false;
        else                field += c;
      }
      else if( c == '"' ) quoted = true;
      else if( c == ',' ){ fields.push_back( Trim( field ) ); field.clear( ); }
      else                field += c;
    }
    fields.push_back( Trim( field ) );

    return fields;
  }

  // A name ROOT, TBrowser and a C++ macro all accept: letters, digits and
  // underscores, not starting with a digit, and not used twice in a directory.
  std::string RootName( const std::string& text, std::set<std::string>& used )
  {
    std::string name;
    for( char c : text )
      name += std::isalnum( static_cast<unsigned char>( c ) ) ? c : '_';

    // Squeeze the runs of underscores left by "Terminal Voltage (kV)" & co.
    name.erase( std::unique( name.begin( ), name.end( ),
                             []( char a, char b ){ return a == '_' && b == '_'; } ),
                name.end( ) );
    name = Trim( name, "_" );

    if( name.empty( ) ) name = "Metric";
    if( std::isdigit( static_cast<unsigned char>( name[0] ) ) ) name = "_" + name;

    std::string unique = name;
    for( int n = 2; used.count( unique ); ++n ) unique = name + "_" + std::to_string( n );
    used.insert( unique );

    return unique;
  }

  // Writes and forgets a sub directory. Its key list is saved now, so that a
  // crash later on still leaves it readable; and it must not stay in the list
  // of the file, because a TTree roll-over to a new file moves every object of
  // that list to the new file, a directory of the old one included.
  void CloseDirectory( TDirectory* dir )
  {
    dir->SaveSelf( kTRUE );
    delete dir;
  }

}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

void RunContext::Load( const std::string& directory )
{
  const std::string dir = directory.empty( ) ? std::string( "." ) : directory;

  std::cout << "Looking for the run context (" << kCurrentFile << ", " << kStatsFile
            << ", " << kMetadataFile << ") in '" << dir << "'..." << std::endl;

  fHasCurrent  = LoadCurrent ( dir + "/" + kCurrentFile  );
  fHasStats    = LoadStats   ( dir + "/" + kStatsFile    );
  fHasMetadata = LoadMetadata( dir + "/" + kMetadataFile );
}

// current.txt, as the TetrAMM and RBD 9103 loggers write it:
//
//   ### Start time: 2026-10-21 10:24:02.730670 ###
//   # Time(s)	Current(uA)
//   1.95306063e-01	4.500e-06
//
// with one current column per channel of the module. The parsing and the
// integral follow read_current and integrate_current of WebDAQ
// (server/app/services/run_data.py) line by line, so that both report the same
// charge for the same file.
bool RunContext::LoadCurrent( const std::string& path )
{
  std::ifstream input( path.c_str( ) );
  if( !input.is_open( ) ){
    std::cout << "  " << kCurrentFile << ": not found, skipping." << std::endl;
    return false;
  }

  std::vector<std::string>         columns;
  std::vector<std::vector<double>> rows;

  std::string line;
  while( std::getline( input, line ) ){

    line = Trim( line );
    if( line.empty( ) ) continue;

    if( StartsWith( line, "###" ) ){
      const size_t at = line.find( "Start time:" );
      if( at != std::string::npos ) fCurrentStart = Trim( line.substr( at + 11 ), " #\t" );
      continue;
    }

    if( line[0] == '#' ){
      // The column names are tab separated: '# Time(s)<TAB>Current(uA)'.
      columns.clear( );
      std::istringstream header( Trim( line, " \t#" ) );
      std::string column;
      while( std::getline( header, column, '\t' ) )
        if( !Trim( column ).empty( ) ) columns.push_back( Trim( column ) );
      continue;
    }

    std::istringstream fields( line );
    std::vector<double> row;
    std::string token;
    bool ok = true;
    while( fields >> token ){
      double value = 0;
      if( !ParseNumber( token, value ) ){ ok = false; break; }
      row.push_back( value );
    }
    if( !ok || row.size( ) < 2 ) continue;     // a partially written last line

    rows.push_back( row );
  }

  if( rows.empty( ) ){
    std::cout << "  " << kCurrentFile << ": no samples, skipping." << std::endl;
    return false;
  }

  size_t nChannels = rows[0].size( ) - 1;
  for( const std::vector<double>& row : rows ) nChannels = std::min( nChannels, row.size( ) - 1 );

  if( columns.size( ) != nChannels + 1 ){
    columns.assign( 1, "Time(s)" );
    for( size_t c = 0; c < nChannels; ++c ) columns.push_back( "Ch" + std::to_string( c ) + "(uA)" );
  }

  fCurrentTimes.clear( );
  for( const std::vector<double>& row : rows ) fCurrentTimes.push_back( row[0] );

  fCurrents.assign( nChannels, Series( ) );
  for( size_t c = 0; c < nChannels; ++c ){
    Series& series = fCurrents[c];
    series.name  = ( c == 0 ) ? "Current" : "Current_" + std::to_string( c );
    series.title = columns[c+1];
    series.unit  = "uA";
    series.values.reserve( rows.size( ) );
    for( const std::vector<double>& row : rows ) series.values.push_back( row[c+1] );
  }

  // Trapezoidal rule. The current is in uA against seconds, so the integral is
  // in uC. A step that does not move forward (a clock jump, a duplicated time
  // stamp) is skipped rather than counted as negative charge.
  fCharges.assign( nChannels, 0.0 );
  for( size_t i = 1; i < rows.size( ); ++i ){
    const double dt = rows[i][0] - rows[i-1][0];
    if( dt <= 0 ) continue;
    for( size_t c = 0; c < nChannels; ++c )
      fCharges[c] += 0.5 * ( rows[i][c+1] + rows[i-1][c+1] ) * dt;
  }
  fCurrentDuration = rows.size( ) > 1 ? rows.back( )[0] - rows.front( )[0] : 0.0;

  std::cout << "  " << kCurrentFile << ": " << rows.size( ) << " sample(s), "
            << nChannels << " current column(s), " << fCurrentDuration << " s";
  if( rows.size( ) > 1 ) std::cout << ", accumulated charge " << fCharges[0] << " uC";
  std::cout << std::endl;

  return true;
}

// stats.csv, as the Stats page of WebDAQ records it:
//
//   # Start time: 2026-10-21T10:24:02
//   # Metric: Terminal Voltage | unit: kV | source: accelerator.terminal_voltage
//   Time [s],Terminal Voltage [kV]
//   0.001,3.5e+02
//
// Every row is written full width, the writer pads a sample it could not get
// with 0, so a short row is the last line of a file caught mid-write and is
// dropped: "4.4,12" is not a sample of 12 but the start of 129.5.
bool RunContext::LoadStats( const std::string& path )
{
  std::ifstream input( path.c_str( ) );
  if( !input.is_open( ) ){
    std::cout << "  " << kStatsFile << ": not found, skipping." << std::endl;
    return false;
  }

  std::vector<Series>              metrics;   // from the '# Metric:' lines
  std::vector<std::string>         columns;
  std::vector<std::vector<double>> rows;
  size_t shortRows = 0;

  std::string line;
  while( std::getline( input, line ) ){

    if( Trim( line ).empty( ) ) continue;

    if( line[0] == '#' ){
      const std::string body = Trim( line, " \t\r\n#" );
      if( StartsWith( body, "Start time:" ) ) fStatsStart = Trim( body.substr( 11 ) );
      else if( StartsWith( body, "Metric:" ) ){
        // 'Metric: <alias> | unit: <unit> | source: <graphite path>'
        Series metric;
        std::istringstream parts( body );
        std::string part;
        for( int index = 0; std::getline( parts, part, '|' ); ++index ){
          const size_t colon = part.find( ':' );
          if( colon == std::string::npos ) continue;
          std::string key = Trim( part.substr( 0, colon ) );
          std::transform( key.begin( ), key.end( ), key.begin( ), ::tolower );
          const std::string value = Trim( part.substr( colon + 1 ) );
          if     ( index == 0 && key == "metric" ) metric.title  = value;
          else if( key == "unit"   )               metric.unit   = value;
          else if( key == "source" )               metric.source = value;
        }
        // '-' is how the writer says "no unit".
        if( metric.unit == "-" ) metric.unit.clear( );
        metrics.push_back( metric );
      }
      continue;
    }

    const std::vector<std::string> fields = SplitCsv( Trim( line ) );

    // The first line that is not a comment names the columns.
    if( columns.empty( ) ){ columns = fields; continue; }

    std::vector<double> row;
    bool ok = true;
    for( const std::string& field : fields ){
      double value = 0;
      if( !ParseNumber( field, value ) ){ ok = false; break; }
      row.push_back( value );
    }
    if( !ok ){ ++shortRows; continue; }
    if( row.size( ) < columns.size( ) ){ ++shortRows; continue; }
    // A longer row (a metric added to a file already open) keeps what the
    // header names.
    row.resize( columns.size( ) );
    rows.push_back( row );
  }

  if( columns.size( ) < 2 ){
    std::cout << "  " << kStatsFile << ": no metric column, skipping." << std::endl;
    return false;
  }
  if( rows.empty( ) ){
    std::cout << "  " << kStatsFile << ": no complete sample, skipping." << std::endl;
    return false;
  }

  const size_t nMetrics = columns.size( ) - 1;

  // Without one '# Metric:' line per column, the column headings are all there
  // is to go by.
  if( metrics.size( ) != nMetrics ){
    metrics.assign( nMetrics, Series( ) );
    for( size_t m = 0; m < nMetrics; ++m ) metrics[m].title = columns[m+1];
  }

  fStatsTimes.clear( );
  for( const std::vector<double>& row : rows ) fStatsTimes.push_back( row[0] );

  std::set<std::string> used = { "StartTime" };
  for( size_t m = 0; m < nMetrics; ++m ){
    Series& metric = metrics[m];
    metric.name = RootName( metric.title, used );
    used.insert( metric.name + "_source" );
    metric.values.reserve( rows.size( ) );
    for( const std::vector<double>& row : rows ) metric.values.push_back( row[m+1] );
  }
  fMetrics = metrics;

  std::cout << "  " << kStatsFile << ": " << rows.size( ) << " sample(s) of "
            << nMetrics << " metric(s)";
  if( shortRows ) std::cout << ", " << shortRows << " incomplete row(s) dropped";
  std::cout << std::endl;

  return true;
}

// metadata.json is kept verbatim: it is the run record and whoever reads it
// back wants all of it, as WebDAQ wrote it. The only value taken out of it is
// the accumulated charge, so that it can be set next to the one integrated
// here without a JSON parser.
bool RunContext::LoadMetadata( const std::string& path )
{
  std::ifstream input( path.c_str( ) );
  if( !input.is_open( ) ){
    std::cout << "  " << kMetadataFile << ": not found, skipping." << std::endl;
    return false;
  }

  std::ostringstream text;
  text << input.rdbuf( );
  fMetadata = text.str( );

  if( Trim( fMetadata ).empty( ) ){
    std::cout << "  " << kMetadataFile << ": empty, skipping." << std::endl;
    return false;
  }

  const std::string key = "\"Accumulated Charge\"";
  const size_t at = fMetadata.find( key );
  if( at != std::string::npos ){
    size_t pos = fMetadata.find_first_not_of( " \t\r\n", at + key.size( ) );
    if( pos != std::string::npos && fMetadata[pos] == ':' ){
      const char* start = fMetadata.c_str( ) + pos + 1;
      char* end = nullptr;
      const double value = std::strtod( start, &end );
      // null, a string or anything else that is not a number is left out.
      if( end != start ){
        fMetadataCharge    = value;
        fHasMetadataCharge = true;
      }
    }
  }

  std::cout << "  " << kMetadataFile << ": " << fMetadata.size( ) << " bytes";
  if( fHasMetadataCharge ) std::cout << ", Accumulated Charge " << fMetadataCharge;
  std::cout << std::endl;

  return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

void RunContext::Write( TDirectory* where ) const
{
  if( !where || Empty( ) ) return;

  TDirectory* const saved = gDirectory;

  if( fHasMetadata ){
    TNamed metadata( "RunMetadata", fMetadata.c_str( ) );
    where->WriteTObject( &metadata, nullptr, "Overwrite" );

    if( fHasMetadataCharge ){
      TParameter<double> charge( "AccumulatedCharge_metadata", fMetadataCharge );
      where->WriteTObject( &charge, nullptr, "Overwrite" );
    }
  }

  if( fHasCurrent ){
    TDirectory* dir = where->mkdir( "Current", "Beam current (current.txt)", kTRUE );

    for( size_t c = 0; c < fCurrents.size( ); ++c ){
      const Series& series = fCurrents[c];

      TGraph graph( static_cast<int>( fCurrentTimes.size( ) ),
                    fCurrentTimes.data( ), series.values.data( ) );
      graph.SetName( series.name.c_str( ) );
      graph.SetTitle( series.title.c_str( ) );
      graph.GetXaxis( )->SetTitle( kTimeTitle );
      graph.GetYaxis( )->SetTitle( "Current [#muA]" );
      dir->WriteTObject( &graph, nullptr, "Overwrite" );

      // The charge needs two samples at least, as in WebDAQ.
      if( fCurrentTimes.size( ) < 2 ) continue;

      const std::string suffix = ( c == 0 ) ? "" : "_" + std::to_string( c );
      TParameter<double> charge( ( "AccumulatedCharge_uC" + suffix ).c_str( ), fCharges[c] );
      TParameter<double> mean  ( ( "MeanCurrent_uA" + suffix ).c_str( ),
                                 fCurrentDuration > 0 ? fCharges[c] / fCurrentDuration : 0.0 );
      dir->WriteTObject( &charge, nullptr, "Overwrite" );
      dir->WriteTObject( &mean,   nullptr, "Overwrite" );
    }

    TParameter<double> duration( "Duration_s", fCurrentDuration );
    dir->WriteTObject( &duration, nullptr, "Overwrite" );

    if( !fCurrentStart.empty( ) ){
      TNamed start( "StartTime", fCurrentStart.c_str( ) );
      dir->WriteTObject( &start, nullptr, "Overwrite" );
    }

    CloseDirectory( dir );
  }

  if( fHasStats ){
    TDirectory* dir = where->mkdir( "Stats", "Monitored metrics (stats.csv)", kTRUE );

    for( const Series& metric : fMetrics ){

      const std::string title = metric.unit.empty( ) ? metric.title
                                                     : metric.title + " [" + metric.unit + "]";

      TGraph graph( static_cast<int>( fStatsTimes.size( ) ),
                    fStatsTimes.data( ), metric.values.data( ) );
      graph.SetName( metric.name.c_str( ) );
      graph.SetTitle( title.c_str( ) );
      graph.GetXaxis( )->SetTitle( kTimeTitle );
      graph.GetYaxis( )->SetTitle( title.c_str( ) );
      dir->WriteTObject( &graph, nullptr, "Overwrite" );

      if( !metric.source.empty( ) ){
        TNamed source( ( metric.name + "_source" ).c_str( ), metric.source.c_str( ) );
        dir->WriteTObject( &source, nullptr, "Overwrite" );
      }
    }

    if( !fStatsStart.empty( ) ){
      TNamed start( "StartTime", fStatsStart.c_str( ) );
      dir->WriteTObject( &start, nullptr, "Overwrite" );
    }

    CloseDirectory( dir );
  }

  if( saved ) saved->cd( );
}
