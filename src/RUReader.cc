#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include <TNamed.h>
#include <TVectorD.h>

#include "RUReader.h"
#include "Utils.h"

namespace {

  // Bits of the "Extras" field of the PHA energy word.
  const uint32_t kPhaSatuBit     = 0x01;   // input saturation
  const uint32_t kPhaRollOverBit = 0x02;   // time stamp roll over (fake event)
  const uint32_t kPhaFakeBit     = 0x04;   // fake event
  const uint32_t kPhaLostBit     = 0x20;   // lost event

  // Bits of the PSD extra word when it carries the flags (extras mode 1).
  const uint32_t kPsdLostBit = 0x00001000;
  const uint32_t kPsdOverBit = 0x00004000;

  const int kMaxWrapMessages   = 20;
  const int kMaxHeaderWords    = 1024;     // sanity limit for the XDAQ header
  const uint64_t kMaxBufferWords = ( 1024ull * 1024 * 1024 ) / sizeof(uint32_t);

  uint64_t TimeUnitPicoseconds( TimeUnit unit )
  {
    switch( unit ){
      case TimeUnit::Picosecond:  return 1ull;
      case TimeUnit::Nanosecond:  return 1000ull;
      case TimeUnit::Microsecond: return 1000000ull;
      case TimeUnit::Millisecond: return 1000000000ull;
      case TimeUnit::Second:      return 1000000000000ull;
      default:                    return 1ull;
    }
  }

  bool FileSize( const std::string& path, uint64_t& size )
  {
    struct stat info;
    if( stat( path.c_str( ), &info ) != 0 ) return false;
    size = static_cast<uint64_t>( info.st_size );
    return true;
  }

}

const char* TimeUnitName( TimeUnit unit )
{
  switch( unit ){
    case TimeUnit::Raw:         return "raw";
    case TimeUnit::Picosecond:  return "ps";
    case TimeUnit::Nanosecond:  return "ns";
    case TimeUnit::Microsecond: return "us";
    case TimeUnit::Millisecond: return "ms";
    case TimeUnit::Second:      return "s";
  }
  return "ps";
}

bool ParseTimeUnit( const std::string& text, TimeUnit& unit )
{
  if( text == "raw"                       ){ unit = TimeUnit::Raw;         return true; }
  if( text == "ps" || text == "picosecond"){ unit = TimeUnit::Picosecond;  return true; }
  if( text == "ns" || text == "nanosecond"){ unit = TimeUnit::Nanosecond;  return true; }
  if( text == "us" || text == "microsecond"){unit = TimeUnit::Microsecond; return true; }
  if( text == "ms" || text == "millisecond"){unit = TimeUnit::Millisecond; return true; }
  if( text == "s"  || text == "second"    ){ unit = TimeUnit::Second;      return true; }
  return false;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RUReader::RUReader( std::map<int,std::string> name, bool ignoreFail, bool forceDual,
                    bool ignorePsdBoards )
  : fUserNames( name ),
    fIgnoreFail( ignoreFail ),
    fForceDual( forceDual ),
    fIgnorePsdBoards( ignorePsdBoards )
{
  // The trace containers are allocated once and resized in place, so that the
  // decoding of a waveform does not allocate anything per event.
  fWave1     = new TArrayS( 0 );
  fWave2     = new TArrayS( 0 );
  fDigital11 = new TArrayS( 0 );
  fDigital12 = new TArrayS( 0 );
  fDigital21 = new TArrayS( 0 );
  fDigital22 = new TArrayS( 0 );

  if( fIgnoreFail )
    std::cout << "--ignore-fail found! RUReader will ignore board fails!" << std::endl;
  if( fForceDual )
    std::cout << "--force-dual-trace found! RUReader will create two waveforms in the TTree!" << std::endl;
  if( fIgnorePsdBoards )
    std::cout << "--ignore-psd-boards found! RUReader will not save data from PSD firmware boards to the TTree!" << std::endl;
}

RUReader::~RUReader( )
{
  delete fWave1;
  delete fWave2;
  delete fDigital11;
  delete fDigital12;
  delete fDigital21;
  delete fDigital22;
}

// ---------------------------------------------------------------------------
// ROOT output
// ---------------------------------------------------------------------------

void RUReader::InitializeROOT( )
{
  fFileOut = new TFile( fFileOutName.c_str( ), "RECREATE" );
  if( fCompression >= 0 ) fFileOut->SetCompressionLevel( fCompression );

  bool hasPHA = false;
  bool hasPSD = false;
  for( int b = 0; b < kMaxBoards; ++b ){
    if( !fBoards[b].present ) continue;
    if( fBoards[b].dppType == CAEN_DGTZ_DPPFirmware_PHA ) hasPHA = true;
    if( fBoards[b].dppType == CAEN_DGTZ_DPPFirmware_PSD ) hasPSD = true;
  }

  fTree = new TTree( "Data", "Unified DAQ Data" );

  // Branches common to PHA and PSD.
  fTree->Branch( "PU"       ,        &fPu,        "PU/O" );
  fTree->Branch( "SATU"     ,      &fSatu,      "SATU/O" );
  fTree->Branch( "LOST"     ,      &fLost,      "LOST/O" );
  fTree->Branch( "Board"    ,     &fBoard,     "Board/s" );
  fTree->Branch( "Channel"  ,   &fChannel,   "Channel/s" );
  fTree->Branch( "Timestamp", &fTimeStamp, "Timestamp/l" ); // CoMPASS spells it Timestamp
  fTree->Branch( "Flags"    ,    &fExtras,     "Flags/i" );
  fTree->Branch( "Extras2"  ,   &fExtras2,   "Extras2/i" );

  if( hasPHA )
    fTree->Branch( "Energy", &fEnergies, "Energy/s" );

  if( hasPSD ){
    fTree->Branch( "EnergyShort", &fQShort, "EnergyShort/s" );
    fTree->Branch( "EnergyLong" ,  &fQLong,  "EnergyLong/s" );
  }

  fTree->SetMaxVirtualSize( 2000000000LL );
  fTree->SetMaxTreeSize( 10LL * 1024 * 1024 * 1024 );
}

void RUReader::InitializeWave( bool dualTrace )
{
  if( fWaveInitialized ) return;

  fWaveDual = dualTrace || fForceDual;

  if( fTree->GetEntries( ) > 0 ){
    std::ostringstream msg;
    msg << "WARNING: the first " << fTree->GetEntries( ) << " event(s) carried no trace, "
        << "the waveform branches start later in the tree.";
    ReportLine( msg.str( ) );
  }

  if( fWaveDual ){
    fTree->Branch( "fWave1"     ,     &fWave1 );
    fTree->Branch( "fWave2"     ,     &fWave2 );
    fTree->Branch( "fDigital1_1", &fDigital11 );
    fTree->Branch( "fDigital1_2", &fDigital12 );
    fTree->Branch( "fDigital2_1", &fDigital21 );
    fTree->Branch( "fDigital2_2", &fDigital22 );
  }
  else{
    fTree->Branch( "fWave"    ,     &fWave1 );
    fTree->Branch( "fDigital1", &fDigital11 );
    fTree->Branch( "fDigital2", &fDigital21 );
  }

  fWaveInitialized = true;
}

// ---------------------------------------------------------------------------
// Board identification
// ---------------------------------------------------------------------------

// The XDAQ header carries the sampling period, the time tag period, the number
// of channels and the firmware type, which is enough to tell the board families
// apart. The -d option is still honoured and takes precedence.
std::string RUReader::GuessBoardName( uint32_t nsPerSample, uint32_t nsPerTimetag,
                                      uint32_t channels, CAEN_DGTZ_DPPFirmware_t dpp )
{
  (void)nsPerTimetag;

  if( nsPerSample >= 10 ) return "V1724";                     // 100 MS/s
  if( nsPerSample == 2  ) return "V1730";                     // 500 MS/s

  if( nsPerSample == 4 ){                                     // 250 MS/s
    if( channels > 8 )                          return "V1725";
    if( dpp == CAEN_DGTZ_DPPFirmware_PSD )      return "V1720";
    return "V1725";
  }

  return "";
}

bool RUReader::DecodeBoardInfo( uint32_t boardInfo, int index )
{
  const uint32_t nsPerSample  =   boardInfo         & 0x3F;
  const uint32_t nsPerTimetag = ( boardInfo >>  6 ) & 0x3F;
  const uint32_t dppVersion   = ( boardInfo >> 12 ) & 0x0F;
  const uint32_t channels     = ( boardInfo >> 16 ) & 0xFF;
  const uint32_t boardRegId   = ( boardInfo >> 24 ) & 0xFF;

  if( boardRegId < kMaxBoards && fBoards[boardRegId].present ){
    std::cerr << "ERROR: the header describes board id " << boardRegId
              << " more than once." << std::endl;
    return false;
  }

  if( boardRegId >= kMaxBoards ){
    std::cerr << "ERROR: board " << index << " of the header declares id " << boardRegId
              << ", but the aggregate header can only address ids 0-" << kMaxBoards - 1
              << "." << std::endl;
    return false;
  }

  BoardInfo info;
  info.present      = true;
  info.channels     = channels;
  info.nsPerSample  = nsPerSample;
  info.nsPerTimetag = nsPerTimetag;

  switch( dppVersion ){
    case 0: info.dppType = CAEN_DGTZ_DPPFirmware_PHA; break;
    case 1: info.dppType = CAEN_DGTZ_DPPFirmware_PSD; break;
    default:
      std::cerr << "ERROR: board " << boardRegId << " declares the unknown DPP version "
                << dppVersion << " (only 0=PHA and 1=PSD are supported)." << std::endl;
      return false;
  }

  if( channels == 0 || channels > kMaxChannels ){
    std::cerr << "ERROR: board " << boardRegId << " declares " << channels
              << " channels, which is outside the supported range 1-" << kMaxChannels
              << "." << std::endl;
    return false;
  }

  // Name: the command line wins, otherwise it is derived from the header.
  std::map<int,std::string>::const_iterator user = fUserNames.find( static_cast<int>( boardRegId ) );
  if( user != fUserNames.end( ) ){
    info.name         = user->second;
    info.nameFromUser = true;
  }
  else{
    info.name = GuessBoardName( nsPerSample, nsPerTimetag, channels, info.dppType );
    if( info.name.empty( ) ){
      std::cerr << "ERROR: cannot identify board " << boardRegId << " from the header "
                << "(sampling period " << nsPerSample << " ns, " << channels
                << " channels). Please specify it explicitly with -d <name> " << boardRegId
                << "." << std::endl;
      return false;
    }
  }

  // Time stamp unit. The header is authoritative; the board family is only a
  // fallback for headers that leave the field at zero.
  uint32_t nsPerTick = nsPerTimetag;
  if( nsPerTick == 0 ) nsPerTick = nsPerSample;
  if( nsPerTick == 0 ){
    if     ( info.name.find( "724" ) != std::string::npos ||
             info.name.find( "781" ) != std::string::npos ||
             info.name.find( "782" ) != std::string::npos ) nsPerTick = 10;
    else if( info.name.find( "725" ) != std::string::npos ) nsPerTick = 4;
    else if( info.name.find( "730" ) != std::string::npos ) nsPerTick = 2;
    else if( info.name.find( "720" ) != std::string::npos ) nsPerTick = 4;
    else                                                    nsPerTick = 1;

    std::cerr << "WARNING: the header of board " << boardRegId << " does not provide a time "
              << "tag period, assuming " << nsPerTick << " ns from the board type."
              << std::endl;
  }
  info.psPerTick = static_cast<uint64_t>( nsPerTick ) * 1000ull;

  DataFrame frame( info.dppType, info.name );
  frame.Build( );
  if( !frame.IsSupported( ) ){
    std::cerr << "ERROR: board type '" << info.name << "' (id " << boardRegId
              << ") is not supported by the data format builders." << std::endl;
    return false;
  }

  fBoards[boardRegId] = info;
  fFrames[boardRegId] = frame;
  ++fNumBoards;

  return true;
}

void RUReader::PrintBoardTable( ) const
{
  std::cout << std::endl;
  std::cout << "-------------------- XDAQ Header --------------------" << std::endl;
  std::cout << "Number of boards: " << fNumBoards << std::endl;

  for( int b = 0; b < kMaxBoards; ++b ){
    const BoardInfo& info = fBoards[b];
    if( !info.present ) continue;

    std::cout << "Board " << b << ": " << info.name
              << ( info.nameFromUser ? " (from -d)" : " (from header)" )
              << " with " << info.channels << " channels, firmware "
              << ( info.dppType == CAEN_DGTZ_DPPFirmware_PHA ? "PHA" : "PSD" ) << std::endl;
    std::cout << "  - ns per sample:  " << info.nsPerSample << std::endl;
    std::cout << "  - ns per timetag: " << info.nsPerTimetag << std::endl;
    std::cout << "  - time stamp LSB: " << info.psPerTick / 1000.0 << " ns" << std::endl;
  }

  std::cout << "Acquisition start time (epoch): " << fStartTime << std::endl;
  std::cout << "Time stamp written as:          " << TimeUnitName( fTimeUnit ) << std::endl;
  std::cout << "-----------------------------------------------------" << std::endl;
  std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// File header
// ---------------------------------------------------------------------------

// Only the first file of a run carries the XDAQ header, the following ones
// start directly with the board aggregates. This decides where the data of
// such a continuation file begins.
bool RUReader::LocateData( std::ifstream& input, const std::string& fileName,
                           uint64_t fileLength, uint64_t& dataOffset )
{
  dataOffset = 0;

  if( fileLength < sizeof(uint32_t) ){
    std::cerr << "ERROR: '" << fileName << "' is too short to contain any data." << std::endl;
    return false;
  }

  uint32_t first = 0;
  input.seekg( 0, std::ios::beg );
  input.read( reinterpret_cast<char*>( &first ), sizeof(uint32_t) );
  input.clear( );

  // The expected case: the file starts with a board aggregate.
  if( ( first >> 28 ) == 0xA ) return true;

  // Otherwise the writer repeated the header, so parse and skip it. The board
  // descriptions of the first file stay in use.
  std::cerr << "\nNOTE: '" << fileName << "' does not start with a board aggregate, "
            << "assuming it repeats the XDAQ header." << std::endl;

  if( fileLength < 6 * sizeof(uint32_t) || first < 6 ||
      first > kMaxHeaderWords || static_cast<uint64_t>( first ) * sizeof(uint32_t) > fileLength ){
    std::cerr << "ERROR: '" << fileName << "' starts neither with a board aggregate nor "
              << "with a usable header (first word 0x" << std::hex << first << std::dec
              << ")." << std::endl;
    return false;
  }

  dataOffset = static_cast<uint64_t>( first ) * sizeof(uint32_t);
  return true;
}

bool RUReader::ReadHeader( std::ifstream& input, const std::string& fileName,
                           uint64_t fileLength, uint64_t& dataOffset )
{
  dataOffset = 0;

  if( fileLength < 6 * sizeof(uint32_t) ){
    std::cerr << "ERROR: '" << fileName << "' is too short to contain an XDAQ header."
              << std::endl;
    return false;
  }

  uint32_t hSize = 0;
  input.seekg( 0, std::ios::beg );
  input.read( reinterpret_cast<char*>( &hSize ), sizeof(uint32_t) );
  if( !input ){
    std::cerr << "ERROR: cannot read the header size of '" << fileName << "'." << std::endl;
    return false;
  }

  if( hSize < 6 || hSize > kMaxHeaderWords ||
      static_cast<uint64_t>( hSize ) * sizeof(uint32_t) > fileLength ){
    std::cerr << "ERROR: '" << fileName << "' declares an implausible header size of "
              << hSize << " words. The file does not look like a LunaDAQ .caendat file."
              << std::endl;
    return false;
  }

  std::vector<uint32_t> header( hSize, 0 );
  input.seekg( 0, std::ios::beg );
  input.read( reinterpret_cast<char*>( header.data( ) ), hSize * sizeof(uint32_t) );
  if( static_cast<uint64_t>( input.gcount( ) ) != hSize * sizeof(uint32_t) ){
    std::cerr << "ERROR: the header of '" << fileName << "' is truncated." << std::endl;
    return false;
  }
  input.clear( );

  const uint32_t nboards = header[2];
  dataOffset = static_cast<uint64_t>( hSize ) * sizeof(uint32_t);

  if( nboards == 0 ){
    std::cerr << "ERROR: the header of '" << fileName << "' declares no board. "
              << "Without board descriptions the aggregates cannot be decoded." << std::endl;
    return false;
  }

  if( nboards > kMaxBoards || hSize < 6 + nboards ){
    std::cerr << "ERROR: the header of '" << fileName << "' declares " << nboards
              << " boards, which does not fit in a header of " << hSize << " words."
              << std::endl;
    return false;
  }

  fStartTime = header[5];

  for( uint32_t i = 0; i < nboards; ++i )
    if( !DecodeBoardInfo( header[6+i], static_cast<int>( i ) ) ) return false;

  if( fNumBoards == 0 ){
    std::cerr << "ERROR: no usable board description found in '" << fileName << "'."
              << std::endl;
    return false;
  }

  PrintBoardTable( );
  InitializeROOT( );

  return true;
}

// ---------------------------------------------------------------------------
// Board aggregate header
// ---------------------------------------------------------------------------

void RUReader::UnpackHeader( const uint32_t* buffer, uint32_t offset,
                             uint32_t& aggLength, uint32_t& board,
                             std::bitset<8>& channelMask )
{
  aggLength   =   buffer[offset]   & 0x0FFFFFFF;
  board       = ( buffer[offset+1] & 0xF8000000 ) >> 27;
  channelMask =   buffer[offset+1] & 0xFF;

  const bool     boardFail        = ( buffer[offset+1] >> 26 ) & 0x1;
  const uint32_t aggregateCounter =   buffer[offset+2] & 0x7FFFFF;
  const uint32_t aggregateTimeTag =   buffer[offset+3];

  if( boardFail ){
    ++fStats.failedBoards;

    if( !fIgnoreFail ){
      std::ostringstream msg;
      msg << "\n*** WARNING: Board with ID " << board << " has failed! ***\n"
          << "Time stamps of following events might be misleading.";
      ReportLine( msg.str( ) );

      // Only ask when there is somebody to answer, so that batch conversions
      // never block on a board failure.
      if( isatty( fileno( stdin ) ) ){
        std::cout << "Do you want to continue? (y/n): ";
        char choice = 'n';
        std::cin >> choice;

        if( choice == 'y' || choice == 'Y' ){
          fIgnoreFail = true;
          std::cout << "Continuing with ignore_fail set. Future failures will be ignored."
                    << std::endl << std::endl;
        }
        else{
          std::cout << "Conversion stopped by user." << std::endl;
          this->Write( );
          exit( 1 );
        }
      }
      else{
        fIgnoreFail = true;
        ReportLine( "stdin is not a terminal, continuing as if --ignore-fail had been given." );
      }
    }
  }

  if( fVerbose > 1 ){
    std::ostringstream msg;
    msg << "Aggregate Length: " << aggLength << ", Board: " << board
        << ", Channel Mask: " << channelMask.to_string( )
        << ", Counter: " << aggregateCounter << ", Time Tag: " << aggregateTimeTag;
    ReportLine( msg.str( ) );
  }
}

// ---------------------------------------------------------------------------
// Time stamp reconstruction
// ---------------------------------------------------------------------------

uint64_t RUReader::BuildTimeStamp( uint32_t board, uint32_t channel, uint64_t rawTS,
                                   uint32_t extendedTS, int extendedBits,
                                   bool rollOverFlag, uint16_t fineTS, int fineBits )
{
  const DataLayout& layout = fFrames[board].Layout( );
  ChannelState& state = fChannels[board][channel];

  const bool hasExtended = extendedBits > 0;

  uint64_t raw   = rawTS;
  int      width = layout.tsBits;

  if( hasExtended ){
    raw   = ( static_cast<uint64_t>( extendedTS ) << layout.tsBits ) | rawTS;
    width = layout.tsBits + extendedBits;
  }

  if( rollOverFlag ) ++state.rollOverFlags;

  // A counter of `width` bits restarts from zero every `range` counts. A time
  // stamp that jumps backwards by more than half of the range is such a reset,
  // a smaller step backwards is an out of order event and is only reported.
  const uint64_t range = ( width >= 64 ) ? 0 : ( 1ull << width );

  if( state.seen && raw < state.lastRaw ){
    if( range != 0 && ( state.lastRaw - raw ) > range / 2 ){
      ++state.wraps;
      ++fStats.timeStampWraps;

      if( fWrapMessages < kMaxWrapMessages ){
        ++fWrapMessages;
        std::ostringstream msg;
        msg << "*** Time stamp reset on board " << board << " channel " << channel
            << ": " << state.lastRaw << " -> " << raw << " (" << width
            << " bit counter, reset #" << state.wraps << "). "
            << "The missing " << width << " bits are added back.";
        if( fWrapMessages == kMaxWrapMessages )
          msg << "\n*** Further resets are only counted, see the final statistics.";
        ReportLine( msg.str( ) );
      }
    }
    else{
      ++state.backwardJumps;
      ++fStats.backwardStamps;
    }
  }

  state.lastRaw = raw;
  state.seen    = true;

  // Without the extended time stamp the board signals every reset of the
  // counter with a roll-over flag, which also survives long gaps between two
  // events on the same channel. With it, the software detection is used.
  uint64_t wraps = state.wraps;
  if( !hasExtended && state.rollOverFlags > wraps ) wraps = state.rollOverFlags;

  const uint64_t full = ( range == 0 ) ? raw : raw + wraps * range;

  if( fTimeUnit == TimeUnit::Raw ) return full;

  const uint64_t psPerTick = fBoards[board].psPerTick;
  uint64_t picoseconds = full * psPerTick;
  if( fineBits > 0 )
    picoseconds += ( static_cast<uint64_t>( fineTS ) * psPerTick ) >> fineBits;

  return picoseconds / TimeUnitPicoseconds( fTimeUnit );
}

// ---------------------------------------------------------------------------
// DPP-PHA
// ---------------------------------------------------------------------------

void RUReader::UnpackPHA( const uint32_t* buffer, uint32_t board, std::bitset<8> channelMask,
                          uint32_t offset, uint32_t aggLength )
{
  DataFrame& frame = fFrames[board];

  // On 16 channel boards the channel mask addresses couples, the channel inside
  // the couple is encoded in the time stamp word.
  uint8_t couples[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  int nCouples = 0;
  for( int bit = 0; bit < 8; ++bit )
    if( channelMask.test( bit ) ) couples[nCouples++] = static_cast<uint8_t>( bit );

  const uint32_t aggEnd = offset + aggLength;
  uint32_t pos = offset + kAggHeaderWords;

  for( int cpl = 0; cpl < nCouples; ++cpl ){

    if( pos + 2 > aggEnd ){ ++fStats.corruptedBuffers; return; }

    const uint32_t coupleSize = ExtractBits( buffer[pos], frame.SizeFormat( ) );
    frame.SetDataFormat( buffer[pos+1] );
    const DataLayout& layout = frame.Layout( );

    // The channel aggregates are chained: once one of them is inconsistent the
    // position of the next one is unknown and the rest of the board aggregate
    // has to be dropped.
    if( coupleSize < 2 || pos + coupleSize > aggEnd || layout.evtSize == 0 ){
      ++fStats.corruptedBuffers;
      return;
    }

    const uint32_t nEvents = ( coupleSize - 2 ) / layout.evtSize;
    const uint32_t base    = pos + 2;

    for( uint32_t evt = 0; evt < nEvents; ++evt ){

      const uint32_t ev = base + evt * layout.evtSize;

      const uint32_t timeWord   = buffer[ev];
      const uint32_t energyWord = buffer[ev + layout.evtSize - 1];

      const uint64_t rawTS  = ExtractBits( timeWord, layout.fmtTS );
      const uint32_t extras = ExtractBits( energyWord, layout.fmtExtras );

      uint32_t channel = couples[cpl];
      if( layout.hasChannelBit ) channel = ( ( timeWord >> 31 ) & 0x1 ) + couples[cpl] * 2;

      if( channel >= kMaxChannels ){ ++fStats.droppedEvents; continue; }

      const bool pileUp   = ( energyWord >> 15 ) & 0x1;
      const bool rollOver = ( extras & kPhaRollOverBit ) != 0;
      const bool lost     = ( extras & kPhaLostBit     ) != 0;
      const bool satu     = ( extras & kPhaSatuBit     ) != 0;

      uint32_t extras2      = 0;
      uint32_t extendedTS   = 0;
      int      extendedBits = 0;
      uint16_t fineTS       = 0;
      int      fineBits     = 0;

      if( layout.hasExtras ){
        extras2 = buffer[ev + layout.evtSize - 2];

        switch( layout.extrasMode ){
          case 0:  // extended time stamp [31:16], baseline*4 [15:0]
          case 1:  // extended time stamp [31:16], flags [15:0]
            extendedTS   = extras2 >> 16;
            extendedBits = 16;
            break;
          case 2:  // extended time stamp [31:16], flags [15:10], fine TS [9:0]
            extendedTS   = extras2 >> 16;
            extendedBits = 16;
            fineTS       = extras2 & 0x3FF;
            fineBits     = 10;
            break;
          default: // 4, 5 and 7 carry counters, not a time stamp
            break;
        }
      }

      fPu        = pileUp;
      fSatu      = satu;
      fLost      = lost;
      fBoard     = static_cast<uint16_t>( board );
      fChannel   = static_cast<uint16_t>( channel );
      fEnergies  = static_cast<uint16_t>( energyWord & 0x7FFF );
      fExtras    = extras;
      fExtras2   = extras2;
      fQShort    = 0;
      fQLong     = 0;
      fTimeStamp = BuildTimeStamp( board, channel, rawTS, extendedTS, extendedBits,
                                   rollOver, fineTS, fineBits );

      if( layout.hasTrace ) UnpackWave( buffer, layout, ev );

      if( fVerbose > 1 ){
        std::ostringstream msg;
        msg << "PHA board " << board << " ch " << channel << " energy " << fEnergies
            << " ts " << fTimeStamp << " flags 0x" << std::hex << extras << std::dec;
        ReportLine( msg.str( ) );
      }

      fTree->Fill( );

      ++fStats.totalEvents;
      if( extras & kPhaFakeBit ) ++fStats.fakeEvents;

      ChannelState& state = fChannels[board][channel];
      ++state.events;
      state.lastStamp = fTimeStamp;
    }

    pos += coupleSize;
  }
}

// ---------------------------------------------------------------------------
// DPP-PSD
// ---------------------------------------------------------------------------

void RUReader::UnpackPSD( const uint32_t* buffer, uint32_t board, std::bitset<8> channelMask,
                          uint32_t offset, uint32_t aggLength )
{
  DataFrame& frame = fFrames[board];

  uint8_t couples[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  int nCouples = 0;
  for( int bit = 0; bit < 8; ++bit )
    if( channelMask.test( bit ) ) couples[nCouples++] = static_cast<uint8_t>( bit );

  const uint32_t aggEnd = offset + aggLength;
  uint32_t pos = offset + kAggHeaderWords;

  for( int cpl = 0; cpl < nCouples; ++cpl ){

    if( pos + 2 > aggEnd ){ ++fStats.corruptedBuffers; return; }

    const uint32_t coupleSize = ExtractBits( buffer[pos], frame.SizeFormat( ) );
    frame.SetDataFormat( buffer[pos+1] );
    const DataLayout& layout = frame.Layout( );

    if( coupleSize < 2 || pos + coupleSize > aggEnd || layout.evtSize == 0 ){
      ++fStats.corruptedBuffers;
      return;
    }

    const uint32_t nEvents = ( coupleSize - 2 ) / layout.evtSize;
    const uint32_t base    = pos + 2;

    for( uint32_t evt = 0; evt < nEvents; ++evt ){

      const uint32_t ev = base + evt * layout.evtSize;

      const uint32_t timeWord   = buffer[ev];
      const uint32_t chargeWord = buffer[ev + layout.evtSize - 1];

      const uint64_t rawTS = ExtractBits( timeWord, layout.fmtTS );

      uint32_t channel = couples[cpl];
      if( layout.hasChannelBit ) channel = ( ( timeWord >> 31 ) & 0x1 ) + couples[cpl] * 2;

      if( channel >= kMaxChannels ){ ++fStats.droppedEvents; continue; }

      const bool     pileUp = ( chargeWord >> 15 ) & 0x1;
      const uint16_t qshort = chargeWord & 0x7FFF;
      const uint16_t qlong  = static_cast<uint16_t>( chargeWord >> 16 );

      uint32_t extras2      = 0;
      uint32_t flags        = 0;
      uint32_t extendedTS   = 0;
      int      extendedBits = 0;
      bool     over         = false;
      bool     lost         = false;
      uint16_t fineTS       = 0;
      int      fineBits     = 0;

      if( layout.hasExtras ){
        extras2 = buffer[ev + layout.evtSize - 2];

        if( layout.extrasConfig ){
          switch( layout.extrasMode ){
            case 0:  // extended time stamp [31:16], baseline*4 [15:0]
              extendedTS   = extras2 >> 16;
              extendedBits = 16;
              break;
            case 1:  // extended time stamp [31:16], flags [15:0]
              extendedTS   = extras2 >> 16;
              extendedBits = 16;
              flags        = extras2 & 0xFFFF;
              over         = ( extras2 & kPsdOverBit ) != 0;
              lost         = ( extras2 & kPsdLostBit ) != 0;
              break;
            case 2:  // extended time stamp [31:16], flags [15:10], fine TS [9:0]
              extendedTS   = extras2 >> 16;
              extendedBits = 16;
              flags        = ( extras2 >> 10 ) & 0x3F;
              fineTS       = extras2 & 0x3FF;
              fineBits     = 10;
              break;
            default: // 4, 5 and 7 carry counters, not a time stamp
              break;
          }
        }
        else{
          // x720: the extra word only holds a 15 bit extended time stamp.
          extendedTS   = extras2 & 0x7FFF;
          extendedBits = 15;
        }
      }

      fPu        = pileUp;
      fSatu      = over;
      fLost      = lost;
      fBoard     = static_cast<uint16_t>( board );
      fChannel   = static_cast<uint16_t>( channel );
      fQShort    = qshort;
      fQLong     = qlong;
      fEnergies  = 0;
      fExtras    = flags;
      fExtras2   = extras2;
      fTimeStamp = BuildTimeStamp( board, channel, rawTS, extendedTS, extendedBits,
                                   false, fineTS, fineBits );

      if( layout.hasTrace ) UnpackWave( buffer, layout, ev );

      if( fVerbose > 1 ){
        std::ostringstream msg;
        msg << "PSD board " << board << " ch " << channel << " qshort " << qshort
            << " qlong " << qlong << " ts " << fTimeStamp;
        ReportLine( msg.str( ) );
      }

      fTree->Fill( );

      ++fStats.totalEvents;

      ChannelState& state = fChannels[board][channel];
      ++state.events;
      state.lastStamp = fTimeStamp;
    }

    pos += coupleSize;
  }
}

// ---------------------------------------------------------------------------
// Waveforms
// ---------------------------------------------------------------------------

void RUReader::UnpackWave( const uint32_t* buffer, const DataLayout& layout, uint32_t pos )
{
  if( !fWaveInitialized ) InitializeWave( layout.dualTrace );

  const int nWords = layout.numSamples / 2;   // two samples per 32 bit word
  if( nWords <= 0 ) return;

  const int sampleLo = layout.fmtSample.first;
  const int sampleHi = layout.fmtSample.second;
  const int dp1Lo    = layout.fmtDP1.first;
  const int dp1Hi    = layout.fmtDP1.second;
  const int dp2Lo    = layout.fmtDP2.first;
  const int dp2Hi    = layout.fmtDP2.second;

  if( layout.dualTrace ){

    // TArrayS::Set() is a no-op when the size does not change, so a run with a
    // constant record length never reallocates.
    fWave1->Set( nWords );
    fWave2->Set( nWords );
    fDigital11->Set( nWords );
    fDigital12->Set( nWords );
    fDigital21->Set( nWords );
    fDigital22->Set( nWords );

    Short_t* w1 = fWave1->GetArray( );
    Short_t* w2 = fWave2->GetArray( );
    Short_t* d11 = fDigital11->GetArray( );
    Short_t* d12 = fDigital12->GetArray( );
    Short_t* d21 = fDigital21->GetArray( );
    Short_t* d22 = fDigital22->GetArray( );

    for( int idx = 0; idx < nWords; ++idx ){
      const uint32_t word = buffer[pos + 1 + idx];
      w1[idx]  = static_cast<Short_t>( ExtractBits( word, sampleLo     , sampleHi      ) );
      w2[idx]  = static_cast<Short_t>( ExtractBits( word, sampleLo + 16, sampleHi + 16 ) );
      d11[idx] = static_cast<Short_t>( ExtractBits( word, dp1Lo        , dp1Hi         ) );
      d12[idx] = static_cast<Short_t>( ExtractBits( word, dp1Lo    + 16, dp1Hi    + 16 ) );
      d21[idx] = static_cast<Short_t>( ExtractBits( word, dp2Lo        , dp2Hi         ) );
      d22[idx] = static_cast<Short_t>( ExtractBits( word, dp2Lo    + 16, dp2Hi    + 16 ) );
    }
  }
  else{

    fWave1->Set( 2 * nWords );
    fDigital11->Set( 2 * nWords );
    fDigital21->Set( 2 * nWords );

    // The second set of containers stays allocated but empty, so that the
    // branches created by --force-dual-trace never write a null pointer.
    fWave2->Set( 0 );
    fDigital12->Set( 0 );
    fDigital22->Set( 0 );

    Short_t* w1 = fWave1->GetArray( );
    Short_t* d1 = fDigital11->GetArray( );
    Short_t* d2 = fDigital21->GetArray( );

    for( int idx = 0; idx < nWords; ++idx ){
      const uint32_t word = buffer[pos + 1 + idx];
      w1[2*idx]     = static_cast<Short_t>( ExtractBits( word, sampleLo     , sampleHi      ) );
      w1[2*idx + 1] = static_cast<Short_t>( ExtractBits( word, sampleLo + 16, sampleHi + 16 ) );
      d1[2*idx]     = static_cast<Short_t>( ExtractBits( word, dp1Lo        , dp1Hi         ) );
      d1[2*idx + 1] = static_cast<Short_t>( ExtractBits( word, dp1Lo    + 16, dp1Hi    + 16 ) );
      d2[2*idx]     = static_cast<Short_t>( ExtractBits( word, dp2Lo        , dp2Hi         ) );
      d2[2*idx + 1] = static_cast<Short_t>( ExtractBits( word, dp2Lo    + 16, dp2Hi    + 16 ) );
    }
  }
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

bool RUReader::ReadData( std::ifstream& input, uint64_t startPos, uint64_t fileLength )
{
  if( fBuffer.size( ) != fBufferWords ) fBuffer.resize( fBufferWords );

  uint64_t pos = startPos;

  while( pos < fileLength ){

    input.seekg( static_cast<std::streamoff>( pos ), std::ios::beg );
    input.read( reinterpret_cast<char*>( fBuffer.data( ) ),
                static_cast<std::streamsize>( fBuffer.size( ) * sizeof(uint32_t) ) );

    const std::streamsize bytesRead = input.gcount( );
    input.clear( );                       // a short read at the end sets eofbit

    if( bytesRead <= 0 ) break;

    const uint32_t wordsAvail = static_cast<uint32_t>( bytesRead / sizeof(uint32_t) );
    const bool     lastChunk  = ( pos + static_cast<uint64_t>( bytesRead ) >= fileLength );

    uint32_t offset   = 0;
    uint32_t consumed = 0;

    while( offset + kAggHeaderWords <= wordsAvail ){

      // A board aggregate always starts with 1010 in the four highest bits.
      if( ( fBuffer[offset] >> 28 ) != 0xA ){
        ++offset;
        ++fStats.resyncWords;
        continue;
      }

      const uint32_t aggLength = fBuffer[offset] & 0x0FFFFFFF;

      if( aggLength < kAggHeaderWords ){
        ++offset;
        ++fStats.corruptedBuffers;
        continue;
      }

      if( offset + aggLength > wordsAvail ){
        // The aggregate is not fully contained in this chunk. Stop here and
        // read again from its first word instead of throwing the tail away.
        break;
      }

      uint32_t board = 0;
      uint32_t length = 0;
      std::bitset<8> channelMask;
      UnpackHeader( fBuffer.data( ), offset, length, board, channelMask );

      ++fStats.totalAggregates;

      if( board >= kMaxBoards || !fBoards[board].present ){
        ++fStats.unknownBoards;
        if( fStats.unknownBoards <= 5 ){
          std::ostringstream msg;
          msg << "WARNING: aggregate for board id " << board
              << ", which is not described in the file header. Skipping it.";
          ReportLine( msg.str( ) );
        }
      }
      else if( fBoards[board].dppType == CAEN_DGTZ_DPPFirmware_PHA ){
        UnpackPHA( fBuffer.data( ), board, channelMask, offset, aggLength );
      }
      else if( fBoards[board].dppType == CAEN_DGTZ_DPPFirmware_PSD ){
        if( !fIgnorePsdBoards )
          UnpackPSD( fBuffer.data( ), board, channelMask, offset, aggLength );
      }

      offset  += aggLength;
      consumed = offset;
    }

    if( consumed == 0 ){

      if( wordsAvail == fBuffer.size( ) && !lastChunk ){
        // A single aggregate is larger than the whole buffer.
        if( fBuffer.size( ) * 2 > kMaxBufferWords ){
          ReportLine( "ERROR: found an aggregate larger than the maximum buffer size." );
          return false;
        }
        fBuffer.resize( fBuffer.size( ) * 2 );
        fBufferWords = fBuffer.size( );
        continue;                          // read the same position again
      }

      // Nothing usable left: the file ends with an unfinished aggregate.
      fStats.truncatedWords += wordsAvail;
      fBytesDone += fileLength - pos;
      break;
    }

    pos        += static_cast<uint64_t>( consumed ) * sizeof(uint32_t);
    fBytesDone += static_cast<uint64_t>( consumed ) * sizeof(uint32_t);

    if( lastChunk && consumed < wordsAvail ){
      // The file ends in the middle of an aggregate. Stop here, otherwise the
      // same tail would be read, and counted, once more.
      fStats.truncatedWords += wordsAvail - consumed;
      fBytesDone += fileLength - pos;
      pos = fileLength;
    }

    UpdateProgress( );
  }

  return true;
}

// ---------------------------------------------------------------------------
// Driving
// ---------------------------------------------------------------------------

bool RUReader::Read( const std::string& in, const std::string& out )
{
  return ReadMultiple( std::vector<std::string>( 1, in ), out );
}

bool RUReader::ReadMultiple( const std::vector<std::string>& inputFiles, const std::string& out )
{
  const size_t lastSlash = out.find_last_of( "/\\" );
  if( lastSlash != std::string::npos ){
    const std::string outDir = out.substr( 0, lastSlash );
    struct stat info;
    if( stat( outDir.c_str( ), &info ) != 0 ){
      std::cerr << "ERROR: Output directory '" << outDir << "' does not exist!" << std::endl;
      return false;
    }
  }

  fFileOutName = out;

  // Collect the files that actually carry data and the total size, so that the
  // progress bar covers the whole conversion and not a single file.
  std::vector<std::string> files;
  std::vector<uint64_t>    sizes;

  for( size_t i = 0; i < inputFiles.size( ); ++i ){
    uint64_t size = 0;
    if( !FileSize( inputFiles[i], size ) ){
      std::cerr << "ERROR: Input file '" << inputFiles[i] << "' does not exist!" << std::endl;
      return false;
    }
    if( size == 0 ){
      std::cerr << "WARNING: Input file '" << inputFiles[i] << "' is empty, skipping..."
                << std::endl;
      continue;
    }
    files.push_back( inputFiles[i] );
    sizes.push_back( size );
    fBytesTotal += size;
  }

  if( files.empty( ) ){
    std::cerr << "ERROR: none of the input files contains any data." << std::endl;
    return false;
  }

  std::cout << "Processing " << files.size( ) << " file(s), total size: "
            << ( fBytesTotal / 1024.0 / 1024.0 ) << " MB" << std::endl;

  fStats.startTime = std::chrono::steady_clock::now( );
  fProgressStart   = fStats.startTime;
  fProgressLast    = fStats.startTime;

  bool ok = true;

  for( size_t idx = 0; idx < files.size( ) && ok; ++idx ){

    std::ifstream input( files[idx].c_str( ), std::ios::binary );
    if( !input.is_open( ) ){
      std::cerr << "ERROR: Could not open input file '" << files[idx] << "' for reading!"
                << std::endl;
      ok = false;
      break;
    }

    if( fVerbose ){
      std::ostringstream msg;
      msg << "Processing file " << ( idx + 1 ) << "/" << files.size( ) << ": " << files[idx]
          << " (" << ( sizes[idx] / 1024.0 / 1024.0 ) << " MB)";
      ReportLine( msg.str( ) );
    }

    // Only the first file of a run carries the XDAQ header with the board
    // descriptions; the following ones continue with the board aggregates.
    uint64_t dataOffset = 0;
    const bool headerOk = ( idx == 0 )
      ? ReadHeader( input, files[idx], sizes[idx], dataOffset )
      : LocateData( input, files[idx], sizes[idx], dataOffset );

    if( !headerOk ){
      ok = false;
      break;
    }

    fBytesDone += dataOffset;

    ok = ReadData( input, dataOffset, sizes[idx] );

    input.close( );
  }

  FinishProgress( );

  fStats.endTime = std::chrono::steady_clock::now( );

  return ok;
}

void RUReader::Write( )
{
  if( !fTree ){
    std::cerr << "Nothing to write: the conversion never started." << std::endl;
    return;
  }

  std::cout << "Saving data to ROOT file..." << std::endl;

  fFileOut = fTree->GetCurrentFile( );
  fFileOut->cd( );

  std::cout << "Writing TTree with " << fTree->GetEntries( ) << " entries..." << std::endl;
  fTree->Write( 0, TObject::kOverwrite );

  // Metadata, so that the time stamps can be interpreted without guessing.
  TVectorD startEpoch( 1 );
  startEpoch[0] = fStartTime;
  startEpoch.Write( "StartEpoch" );

  TNamed unit( "TimestampUnit", TimeUnitName( fTimeUnit ) );
  unit.Write( );

  std::ostringstream boards;
  for( int b = 0; b < kMaxBoards; ++b ){
    if( !fBoards[b].present ) continue;
    boards << b << ":" << fBoards[b].name << ":"
           << ( fBoards[b].dppType == CAEN_DGTZ_DPPFirmware_PHA ? "PHA" : "PSD" ) << ":"
           << fBoards[b].channels << ":" << fBoards[b].psPerTick << "ps;";
  }
  TNamed boardList( "Boards", boards.str( ).c_str( ) );
  boardList.Write( );

  std::cout << "Closing ROOT file..." << std::endl;
  fFileOut->Close( );

  std::cout << "Conversion completed successfully!" << std::endl;
  PrintStatistics( );
}

// ---------------------------------------------------------------------------
// Progress bar
// ---------------------------------------------------------------------------

void RUReader::ReportLine( const std::string& message )
{
  if( fProgressActive ) std::cout << "\r" << std::string( 110, ' ' ) << "\r";
  std::cout << message << std::endl;
  fProgressActive = false;
}

void RUReader::UpdateProgress( )
{
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now( );

  if( fProgressActive &&
      std::chrono::duration_cast<std::chrono::milliseconds>( now - fProgressLast ).count( ) < 100 )
    return;

  fProgressLast = now;

  const double total = fBytesTotal > 0 ? static_cast<double>( fBytesTotal ) : 1.0;
  double fraction = static_cast<double>( fBytesDone ) / total;
  if( fraction > 1.0 ) fraction = 1.0;

  const int barWidth = 40;
  const int filled   = static_cast<int>( barWidth * fraction );

  const double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
                           now - fProgressStart ).count( );
  const double speed = elapsed > 0 ? ( fBytesDone / 1e6 ) / elapsed : 0.0;

  // A local stream keeps the formatting flags of std::cout untouched.
  std::ostringstream bar;
  bar << "[";
  for( int k = 0; k < barWidth; ++k ){
    if     ( k <  filled ) bar << "=";
    else if( k == filled ) bar << ">";
    else                   bar << " ";
  }
  bar << "] " << std::fixed << std::setprecision( 1 ) << ( fraction * 100 ) << "%  "
      << "[ " << ( fBytesDone / 1e6 ) << " / " << ( fBytesTotal / 1e6 ) << " MB ]  "
      << speed << " MB/s   ";

  std::cout << "\r" << bar.str( ) << std::flush;
  fProgressActive = true;
}

void RUReader::FinishProgress( )
{
  if( fProgressActive ){
    fBytesDone = fBytesTotal;
    fProgressLast = std::chrono::steady_clock::time_point( );
    fProgressActive = false;
    UpdateProgress( );
    fProgressActive = false;
    std::cout << std::endl;
  }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

void RUReader::PrintStatistics( ) const
{
  std::cout << "\n" << std::string( 60, '=' ) << std::endl;
  std::cout << "                   CONVERSION STATISTICS" << std::endl;
  std::cout << std::string( 60, '=' ) << std::endl;

  const double seconds =
    std::chrono::duration_cast<std::chrono::duration<double>>( fStats.endTime - fStats.startTime ).count( );

  std::ostringstream out;
  out << std::fixed << std::setprecision( 2 );
  out << "Processing time:      " << seconds << " seconds\n";
  out << "Time stamp unit:      " << TimeUnitName( fTimeUnit ) << "\n";

  out << "\nData Processing:\n";
  out << "  Total aggregates:   " << fStats.totalAggregates << "\n";
  out << "  Total events:       " << fStats.totalEvents << "\n";
  out << "  Corrupted buffers:  " << fStats.corruptedBuffers << "\n";
  out << "  Failed boards:      " << fStats.failedBoards << "\n";
  out << "  Resynchronisations: " << fStats.resyncWords << " word(s) skipped\n";
  out << "  Truncated tail:     " << fStats.truncatedWords << " word(s)\n";
  out << "  Unknown board ids:  " << fStats.unknownBoards << "\n";
  out << "  Dropped events:     " << fStats.droppedEvents << "\n";
  if( fStats.fakeEvents )
    out << "  Roll-over events:   " << fStats.fakeEvents << " (fake events from the boards)\n";

  if( fStats.totalAggregates > 0 )
    out << "  Avg events/agg.:    " << std::setprecision( 1 )
        << static_cast<double>( fStats.totalEvents ) / fStats.totalAggregates << "\n";

  if( seconds > 0 )
    out << "  Processing rate:    " << std::setprecision( 0 )
        << fStats.totalEvents / seconds << " events/sec\n";

  out << std::setprecision( 3 );
  out << "\nEvents per Channel:\n";

  for( int board = 0; board < kMaxBoards; ++board ){

    bool any = false;
    for( int ch = 0; ch < kMaxChannels; ++ch )
      if( fChannels[board][ch].events > 0 ){ any = true; break; }
    if( !any ) continue;

    out << "  Board " << board << " (" << fBoards[board].name << "):\n";

    for( int ch = 0; ch < kMaxChannels; ++ch ){

      const ChannelState& state = fChannels[board][ch];
      if( state.events == 0 ) continue;

      // Convert the last time stamp to a wall clock duration.
      const uint64_t unitPs = ( fTimeUnit == TimeUnit::Raw ) ? fBoards[board].psPerTick
                                                             : TimeUnitPicoseconds( fTimeUnit );
      const double totalSeconds = static_cast<double>( state.lastStamp ) * unitPs * 1e-12;
      const int hours   = static_cast<int>( totalSeconds / 3600 );
      const int minutes = static_cast<int>( ( totalSeconds - hours * 3600 ) / 60 );
      const double rest = totalSeconds - hours * 3600 - minutes * 60;

      out << "    Ch " << std::setw( 2 ) << ch << ": " << std::setw( 10 ) << state.events
          << " events (last TS: " << state.lastStamp
          << " = " << hours << "h " << minutes << "m " << rest << "s)";

      if( state.wraps > 0 || state.rollOverFlags > 0 || state.backwardJumps > 0 ){
        out << "\n           resets: " << state.wraps << " detected";
        if( state.rollOverFlags > 0 ) out << ", " << state.rollOverFlags << " roll-over flag(s)";
        if( state.backwardJumps > 0 ) out << ", " << state.backwardJumps << " backward jump(s)";
      }
      out << "\n";
    }
  }

  const double corruptedRate = fStats.totalAggregates > 0
    ? ( 100.0 * fStats.corruptedBuffers / fStats.totalAggregates ) : 0.0;

  out << std::setprecision( 2 );
  out << "\nData Quality:\n";
  out << "  Corruption rate:    " << corruptedRate << "%\n";
  out << "  Time stamp resets:  " << fStats.timeStampWraps << "\n";
  out << "  Backward stamps:    " << fStats.backwardStamps << "\n";

  if( fStats.failedBoards > 0 )
    out << "  WARNING: " << fStats.failedBoards << " board failure(s) detected!\n";
  if( corruptedRate > 1.0 )
    out << "  WARNING: High corruption rate detected!\n";
  if( fStats.backwardStamps > 0 )
    out << "  WARNING: some events are not in time order, check the board configuration.\n";

  std::cout << out.str( );
  std::cout << std::string( 60, '=' ) << std::endl;
}
