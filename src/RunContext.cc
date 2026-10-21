#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dirent.h>
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

  // A local wall-clock time, "2026-10-21 10:24:02.730670" or
  // "2026-10-21T10:24:02.775837[+02:00]", in seconds on a day count that is the
  // same for every such time (the UTC offset is ignored: all of them are the
  // local time of the DAQ machine). False if it is not one.
  bool ParseWallClock( const std::string& text, long double& seconds )
  {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    double s = 0;
    char sep = 0;
    if( std::sscanf( text.c_str( ), "%d-%d-%d%c%d:%d:%lf", &y, &mo, &d, &sep, &h, &mi, &s ) != 7 )
      return false;
    if( sep != ' ' && sep != 'T' ) return false;
    // Days from the civil date (H. Hinnant's algorithm), no time zone involved.
    y -= mo <= 2;
    const long era = ( y >= 0 ? y : y - 399 ) / 400;
    const long yoe = y - era * 400;
    const long doy = ( 153 * ( mo + ( mo > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = era * 146097 + doe;
    seconds = days * 86400.0L + h * 3600.0L + mi * 60.0L + s;
    return true;
  }

  // The scalar fields at the top level of metadata.json — strings, numbers,
  // true/false/null — in file order. Objects and arrays (the board list...)
  // are skipped: they stay readable in RunMetadata, the file verbatim.
  struct JsonField { std::string key, value; bool isString; };

  std::vector<JsonField> JsonTopLevel( const std::string& json )
  {
    std::vector<JsonField> fields;
    auto readString = [&json]( size_t& i ){        // json[i] == '"' on entry
      std::string out;
      for( ++i; i < json.size( ) && json[i] != '"'; ++i ){
        if( json[i] == '\\' && i + 1 < json.size( ) ) ++i;
        out += json[i];
      }
      return out;                                  // i on the closing quote
    };

    size_t i = json.find( '{' );
    if( i == std::string::npos ) return fields;
    for( ++i; i < json.size( ); ++i ){
      if( json[i] == '}' ) break;
      if( json[i] != '"' ) continue;
      const std::string key = readString( i );
      i = json.find_first_not_of( " \t\r\n", i + 1 );
      if( i == std::string::npos || json[i] != ':' ) break;
      i = json.find_first_not_of( " \t\r\n", i + 1 );
      if( i == std::string::npos ) break;

      if( json[i] == '"' ){
        fields.push_back( { key, readString( i ), true } );
      }
      else if( json[i] == '{' || json[i] == '[' ){
        int depth = 0;                             // skip the whole value
        bool inString = false;
        for( ; i < json.size( ); ++i ){
          const char c = json[i];
          if( inString ){ if( c == '\\' ) ++i; else if( c == '"' ) inString = false; continue; }
          if( c == '"' ) inString = true;
          else if( c == '{' || c == '[' ) ++depth;
          else if( ( c == '}' || c == ']' ) && --depth == 0 ) break;
        }
      }
      else {
        const size_t end = json.find_first_of( ",}\r\n", i );
        fields.push_back( { key, Trim( json.substr( i, end - i ) ), false } );
        i = ( end == std::string::npos ? json.size( ) : end ) - 1;
      }
    }
    return fields;
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

  // The run record first: the charge is counted over the run it describes.
  fHasMetadata = LoadMetadata( dir + "/" + kMetadataFile );
  fHasCurrent  = LoadCurrent ( dir + "/" + kCurrentFile  );
  fHasStats    = LoadStats   ( dir + "/" + kStatsFile    );
  LoadRunFiles( dir );
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

  // The charge belongs to the run from its start to its stop, as WebDAQ
  // counts it live and on its Data page (integrate_current in run_data.py):
  // the readings joined by straight lines, the first held back to the start
  // and the last held up to the stop, readings outside the run counted only
  // inside it. The run times come from metadata.json, in the time of this
  // file; without them the charge spans the first to the last reading. The
  // current is in uA against seconds, so the integral is in uC. A step that
  // does not move forward (a clock jump, a duplicated time stamp) is skipped.
  double lo = rows.front( )[0], hi = rows.back( )[0];
  long double origin = 0, start = 0, stop = 0;   // ~1e11 s: a double would lose the microseconds
  fChargeOverRun = ParseWallClock( fCurrentStart, origin ) && ParseWallClock( fRunStart, start )
                   && ParseWallClock( fRunStop, stop ) && stop > start;
  if( fChargeOverRun ){ lo = static_cast<double>( start - origin ); hi = static_cast<double>( stop - origin ); }

  // A stretch longer than kMaxGap without a reading means the monitor was
  // silent: that time is left out of the charge (and reported), not filled
  // in by a line or a held value — the same rule as WebDAQ (MAX_GAP_S).
  const double kMaxGap = 10.0;
  fUncovered = 0;
  fCharges.assign( nChannels, 0.0 );
  for( size_t i = 1; i < rows.size( ); ++i ){
    const double t0 = rows[i-1][0], t1 = rows[i][0];
    const double dt = t1 - t0;
    if( dt <= 0 ) continue;
    const double s0 = std::max( t0, lo ), s1 = std::min( t1, hi );
    if( s1 <= s0 ) continue;
    if( dt > kMaxGap ){ fUncovered += s1 - s0; continue; }
    for( size_t c = 0; c < nChannels; ++c ){
      const double v0 = rows[i-1][c+1], slope = ( rows[i][c+1] - v0 ) / dt;
      fCharges[c] += 0.5 * ( ( v0 + slope * ( s0 - t0 ) ) + ( v0 + slope * ( s1 - t0 ) ) ) * ( s1 - s0 );
    }
  }
  const double head = lo < rows.front( )[0] ? std::min( rows.front( )[0], hi ) - lo : 0.0;
  const double tail = hi > rows.back( )[0]  ? hi - std::max( rows.back( )[0], lo ) : 0.0;
  if( head > kMaxGap ) fUncovered += head;
  if( tail > kMaxGap ) fUncovered += tail;
  for( size_t c = 0; c < nChannels; ++c ){
    if( head > 0 && head <= kMaxGap ) fCharges[c] += rows.front( )[c+1] * head;
    if( tail > 0 && tail <= kMaxGap ) fCharges[c] += rows.back( )[c+1]  * tail;
  }
  fCurrentDuration = hi > lo ? hi - lo : 0.0;

  std::cout << "  " << kCurrentFile << ": " << rows.size( ) << " sample(s), "
            << nChannels << " current column(s), " << fCurrentDuration << " s";
  if( rows.size( ) > 1 || fChargeOverRun )
    std::cout << ", accumulated charge " << fCharges[0] << " uC over "
              << ( fChargeOverRun ? "the run" : "the readings" );
  std::cout << std::endl;
  if( fUncovered >= 1 )
    std::cout << "  Warning: no current reading for " << fUncovered
              << " s of the run; that time is not in the charge" << std::endl;

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

  for( const JsonField& f : JsonTopLevel( fMetadata ) ){
    fRunFields.push_back( { f.key, f.value, f.isString } );
    if( f.key == "Start Time" && f.isString ) fRunStart = f.value;
    if( f.key == "Stop Time"  && f.isString ) fRunStop  = f.value;
    if( f.key == "Accumulated Charge" && !f.isString ){
      double value = 0;
      // null, or anything else that is not a number, is left out.
      if( ParseNumber( f.value, value ) ){
        fMetadataCharge    = value;
        fHasMetadataCharge = true;
      }
    }
  }

  std::cout << "  " << kMetadataFile << ": " << fMetadata.size( ) << " bytes, "
            << fRunFields.size( ) << " field(s)";
  if( fHasMetadataCharge ) std::cout << ", Accumulated Charge " << fMetadataCharge;
  std::cout << std::endl;

  return true;
}

// Every other .json and .cal file of the run directory, verbatim: WebDAQ
// copies the boards' register dumps, their energy calibrations, the ROIs and
// the coincidence gates there, and the data cannot be interpreted without
// them. Small text files; anything over 16 MB is not one of them and is left.
bool RunContext::LoadRunFiles( const std::string& directory )
{
  DIR* dir = opendir( directory.c_str( ) );
  if( !dir ) return false;

  std::vector<std::string> names;
  while( dirent* entry = readdir( dir ) ){
    const std::string name = entry->d_name;
    auto endsWith = [&name]( const char* suffix ){
      const size_t n = std::strlen( suffix );
      return name.size( ) > n && name.compare( name.size( ) - n, n, suffix ) == 0;
    };
    if( name == kMetadataFile ) continue;            // already in RunMetadata
    if( endsWith( ".json" ) || endsWith( ".cal" ) || endsWith( ".jsonl" ) ) names.push_back( name );
  }
  closedir( dir );
  std::sort( names.begin( ), names.end( ) );

  for( const std::string& name : names ){
    std::ifstream input( ( directory + "/" + name ).c_str( ) );
    if( !input.is_open( ) ) continue;
    std::ostringstream text;
    text << input.rdbuf( );
    if( text.str( ).size( ) > ( 16u << 20 ) ) continue;
    fRunFiles.push_back( { name, text.str( ) } );
  }

  if( !fRunFiles.empty( ) ){
    std::cout << "  run files:";
    for( const File& f : fRunFiles ) std::cout << " " << f.name;
    std::cout << std::endl;
  }
  return !fRunFiles.empty( );
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

    // The same fields one by one, so that a macro reads the target or the
    // terminal voltage without parsing JSON. Each object's title is the field
    // name as WebDAQ wrote it.
    TDirectory* dir = where->mkdir( "Run", "Run record (metadata.json)", kTRUE );
    std::set<std::string> used;
    for( const Field& f : fRunFields ){
      const std::string name = RootName( f.key, used );
      double number = 0;
      if( !f.isString && ParseNumber( f.value, number ) ){
        TParameter<double> value( name.c_str( ), number );
        dir->WriteTObject( &value, nullptr, "Overwrite" );
      }
      else {
        TNamed value( name.c_str( ), f.value.c_str( ) );
        dir->WriteTObject( &value, nullptr, "Overwrite" );
      }
    }
    CloseDirectory( dir );
  }

  if( !fRunFiles.empty( ) ){
    TDirectory* dir = where->mkdir( "RunFiles", "Configuration files of the run, verbatim", kTRUE );
    for( const File& f : fRunFiles ){
      TNamed file( f.name.c_str( ), f.text.c_str( ) );
      dir->WriteTObject( &file, nullptr, "Overwrite" );
    }
    CloseDirectory( dir );
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

      // The charge needs two samples at least, or the run's start and stop.
      if( fCurrentTimes.size( ) < 2 && !fChargeOverRun ) continue;

      const std::string suffix = ( c == 0 ) ? "" : "_" + std::to_string( c );
      TParameter<double> charge( ( "AccumulatedCharge_uC" + suffix ).c_str( ), fCharges[c] );
      TParameter<double> mean  ( ( "MeanCurrent_uA" + suffix ).c_str( ),
                                 fCurrentDuration > 0 ? fCharges[c] / fCurrentDuration : 0.0 );
      dir->WriteTObject( &charge, nullptr, "Overwrite" );
      dir->WriteTObject( &mean,   nullptr, "Overwrite" );
    }

    TParameter<double> duration( "Duration_s", fCurrentDuration );
    dir->WriteTObject( &duration, nullptr, "Overwrite" );
    TNamed span( "ChargeSpan", fChargeOverRun ? "run" : "readings" );
    dir->WriteTObject( &span, nullptr, "Overwrite" );
    TParameter<double> uncovered( "Uncovered_s", fUncovered );
    dir->WriteTObject( &uncovered, nullptr, "Overwrite" );

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
