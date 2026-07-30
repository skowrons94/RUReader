#ifndef RUREADER_H
#define RUREADER_H

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <TArrayS.h>
#include <TFile.h>
#include <TTree.h>

#include <CAENDigitizerType.h>

#include "DataFrame.h"

// Unit of the time stamp written to the ROOT tree.
enum class TimeUnit { Raw, Picosecond, Nanosecond, Microsecond, Millisecond, Second };

const char* TimeUnitName( TimeUnit unit );
bool        ParseTimeUnit( const std::string& text, TimeUnit& unit );

class RUReader {

public:

  static constexpr int      kMaxBoards      = 32;  // the board id is 5 bits wide
  static constexpr int      kMaxChannels    = 16;  // largest DPP board handled here
  static constexpr uint32_t kAggHeaderWords = 4;   // board aggregate header size

  RUReader( std::map<int,std::string> name = std::map<int,std::string>( ),
            bool ignoreFail       = false,
            bool forceDual        = false,
            bool ignorePsdBoards  = false,
            std::map<int,int> waveSelect = {} );
  ~RUReader( );

  void SetTimeUnit( TimeUnit unit )      { fTimeUnit = unit; }
  void SetVerbose( int level )           { fVerbose = level; }
  void SetCompressionAlgorithm( int algo ) { fCompressAlgo = algo; }
  void SetCompression( int level )       { fCompression = level; }
  void SetBufferSize( uint64_t bytes )   { fBufferWords = bytes / sizeof(uint32_t); }

  // Returns false as soon as a file cannot be opened or its header is unusable.
  bool Read( const std::string& in, const std::string& out );
  bool ReadMultiple( const std::vector<std::string>& inputFiles, const std::string& out );

  void Write( );
  void PrintStatistics( ) const;

private:

  // ---------------------------------------------------------------- board info

  struct BoardInfo {
    bool                    present      = false;
    std::string             name;
    bool                    nameFromUser = false;
    CAEN_DGTZ_DPPFirmware_t dppType      = CAEN_DGTZ_NotDPPFirmware;
    uint32_t                channels     = 0;
    uint32_t                nsPerSample  = 0;
    uint32_t                nsPerTimetag = 0;
    uint64_t                psPerTick    = 0;   // time stamp LSB, in picoseconds
  };

  // Per channel state needed to rebuild a monotonic 64 bit time stamp.
  struct ChannelState {
    bool     seen            = false;
    uint64_t lastRaw         = 0;   // last raw counter value, before extension
    uint64_t reference       = 0;   // highest full stamp accepted so far, the
                                    // anchor the next event is placed against
    uint64_t epoch           = 0;   // times the raw counter has wrapped
    uint64_t rollOverMarkers = 0;   // fake roll-over events the board emitted
    bool     trustMarkers    = false; // this channel does emit them
    uint64_t softwareWraps   = 0;   // wraps inferred, where no marker exists
    uint64_t events          = 0;
    uint64_t lastStamp       = 0;   // last time stamp written to the tree
    uint64_t maxStamp        = 0;   // highest one seen. Not the same thing: the
                                    // boards do not sort, so the last event of
                                    // a channel is often not its latest, and it
                                    // is the highest that should match the run
                                    // duration.
  };

  // ------------------------------------------------------------------- reading

  bool ReadHeader( std::ifstream& input, const std::string& fileName,
                   uint64_t fileLength, uint64_t& dataOffset );
  bool LocateData( std::ifstream& input, const std::string& fileName,
                   uint64_t fileLength, uint64_t& dataOffset );
  bool ReadData( std::ifstream& input, uint64_t startPos, uint64_t fileLength );

  void UnpackHeader( const uint32_t* buffer, uint32_t offset,
                     uint32_t& aggLength, uint32_t& board, std::bitset<8>& channelMask );

  void UnpackPHA( const uint32_t* buffer, uint32_t board, std::bitset<8> channelMask,
                  uint32_t offset, uint32_t aggLength );
  void UnpackPSD( const uint32_t* buffer, uint32_t board, std::bitset<8> channelMask,
                  uint32_t offset, uint32_t aggLength );
  void UnpackWave( const uint32_t* buffer, const DataLayout& layout, uint32_t pos, uint32_t board );

  // Rebuilds the full 64 bit time stamp and converts it to the requested unit.
  // extendedBits is 0 when the board does not send an extended time stamp.
  // rollOverMarker says this event IS the board's fake roll-over event, not
  // merely an event that follows one — see kPhaRollOverMarker.
  uint64_t BuildTimeStamp( uint32_t board, uint32_t channel, uint64_t rawTS,
                           uint32_t extendedTS, int extendedBits,
                           bool rollOverMarker, uint16_t fineTS, int fineBits );

  // ------------------------------------------------------------------- writing

  void InitializeROOT( );
  void InitializeWave( );

  // ----------------------------------------------------------------- utilities

  bool DecodeBoardInfo( uint32_t boardInfo, int index );
  static std::string GuessBoardName( uint32_t nsPerSample, uint32_t nsPerTimetag,
                                     uint32_t channels, CAEN_DGTZ_DPPFirmware_t dpp );
  // Time stamp LSB of a board family, in ns. 0 when the type is not known.
  static uint32_t BoardTimeStampLsb( const std::string& name );
  void PrintBoardTable( ) const;
  void ReportLine( const std::string& message );   // prints above the progress bar
  void UpdateProgress( );
  void FinishProgress( );

  // ------------------------------------------------------------- configuration

  std::map<int,std::string> fUserNames;        // board names given with -d
  std::map<int,int> fWaveSelect; // board id -> which physical wave (1 or 2) to keep
                                 // in the tree when that board records two traces
                                 // but --force-dual-trace was not requested.
                                 // Boards absent from the map default to wave 1.
  std::array<BoardInfo,kMaxBoards> fBoards;
  std::array<DataFrame,kMaxBoards> fFrames;
  int fNumBoards = 0;

  int PreferredWave( int board ) const;

  bool fIgnoreFail      = false;
  bool fForceDual       = false;
  bool fIgnorePsdBoards = false;
  int  fVerbose         = 0;
  int  fCompressAlgo    = -1;                  // -1: leave the ROOT default
  int  fCompression     = -1;                  // -1: leave the ROOT default
  TimeUnit fTimeUnit    = TimeUnit::Picosecond;

  uint32_t fStartTime = 0;                     // acquisition start, epoch seconds

  // ---------------------------------------------------------------- ROOT output

  TFile* fFileOut = nullptr;
  TTree* fTree    = nullptr;
  std::string fFileOutName;

  bool     fPu        = false;
  bool     fSatu      = false;
  bool     fLost      = false;
  uint16_t fBoard     = 0;
  uint16_t fChannel   = 0;
  uint64_t fTimeStamp = 0;
  uint16_t fEnergies  = 0;
  uint16_t fQLong     = 0;
  uint16_t fQShort    = 0;
  uint32_t fExtras    = 0;
  uint32_t fExtras2   = 0;

  TArrayS* fWave1     = nullptr;   // analog trace 1
  TArrayS* fWave2     = nullptr;   // analog trace 2 (dual trace only)
  TArrayS* fDigital11 = nullptr;   // digital probe 1 of trace 1
  TArrayS* fDigital12 = nullptr;   // digital probe 1 of trace 2
  TArrayS* fDigital21 = nullptr;   // digital probe 2 of trace 1
  TArrayS* fDigital22 = nullptr;   // digital probe 2 of trace 2

  bool fWaveInitialized = false;
  bool fWaveDual        = false;

  // ------------------------------------------------------------------ decoding

  std::vector<uint32_t> fBuffer;
  uint64_t fBufferWords = ( 64ull * 1024 * 1024 ) / sizeof(uint32_t);

  std::array<std::array<ChannelState,kMaxChannels>,kMaxBoards> fChannels;

  // -------------------------------------------------------------- progress bar

  uint64_t fBytesTotal = 0;
  uint64_t fBytesDone  = 0;
  std::chrono::steady_clock::time_point fProgressStart;
  std::chrono::steady_clock::time_point fProgressLast;
  bool fProgressActive = false;

  // ---------------------------------------------------------------- statistics

  struct Statistics {
    uint64_t totalEvents      = 0;
    uint64_t totalAggregates  = 0;
    uint64_t corruptedBuffers = 0;
    uint64_t failedBoards     = 0;
    uint64_t resyncWords      = 0;   // words skipped while looking for 0xA
    uint64_t truncatedWords   = 0;   // trailing words of an unfinished aggregate
    uint64_t unknownBoards    = 0;
    uint64_t timeStampWraps   = 0;
    uint64_t droppedEvents    = 0;   // events skipped because of a bad size
    uint64_t fakeEvents       = 0;   // roll-over events generated by the boards
    uint64_t deadTimeEvents   = 0;   // events preceded by dead time
    uint64_t saturatedEvents  = 0;   // input stage (and/or trapezoid) saturated
    uint64_t lostTriggerTags  = 0;   // LOST_TRG tags; one per N lost triggers
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
  } fStats;

  int fWrapMessages = 0;             // limits the wrap reports on screen

};

#endif // RUREADER_H
