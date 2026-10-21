#ifndef RUNCONTEXT_H_INCLUDED
#define RUNCONTEXT_H_INCLUDED

#include <string>
#include <vector>

class TDirectory;

// The files WebDAQ writes next to the raw data of a run (data/run<N>/), read
// so that the ROOT file carries the conditions the data was taken in:
//
//   current.txt    beam current against time since the start of the run
//   stats.csv      the metrics monitored during the run, sampled from Graphite
//   metadata.json  the run record (target, voltages, start and stop time...)
//
// Every file is optional. All three are small next to the data, are read once
// before the conversion starts and written at the very beginning of the output
// file, so that a conversion that dies half way still leaves them behind.
class RunContext {

public:

  // Reads whatever exists in the directory. A missing file is not an error.
  void Load( const std::string& directory );

  bool Empty( ) const { return !fHasCurrent && !fHasStats && !fHasMetadata; }

  // Writes the objects below, into the given directory of the output file:
  //
  //   RunMetadata                   TNamed, metadata.json verbatim
  //   AccumulatedCharge_metadata    TParameter<double>, "Accumulated Charge" of
  //                                 metadata.json, in the unit WebDAQ stores it
  //   Current/Current[_n]           TGraph, current [uA] vs time [s], per column
  //   Current/AccumulatedCharge_uC[_n], MeanCurrent_uA[_n], Duration_s, StartTime,
//                                 ChargeSpan ("run": from the run's start to its
//                                 stop, as WebDAQ counts it; "readings": first
//                                 to last reading, when the run times are unknown)
  //   Stats/<alias>                 TGraph per metric, titled "<alias> [unit]"
  //   Stats/<alias>_source          TNamed, the Graphite path of the metric
  //   Stats/StartTime               TNamed
  void Write( TDirectory* where ) const;

private:

  struct Series {
    std::string         name;     // ROOT object name
    std::string         title;    // what the column was called in the file
    std::string         unit;
    std::string         source;   // Graphite path, stats.csv only
    std::vector<double> values;
  };

  bool LoadCurrent ( const std::string& path );
  bool LoadRunFiles( const std::string& directory );
  bool LoadStats   ( const std::string& path );
  bool LoadMetadata( const std::string& path );

  // ------------------------------------------------------------ current.txt

  bool                fHasCurrent = false;
  std::string         fCurrentStart;           // '### Start time: ... ###'
  std::vector<double> fCurrentTimes;
  std::vector<Series> fCurrents;
  std::vector<double> fCharges;                // uC (uA x s), one per column
  double              fCurrentDuration = 0;    // span the charge covers, s
  bool                fChargeOverRun = false;  // the span is the run, not the readings

  // -------------------------------------------------------------- stats.csv

  bool                fHasStats = false;
  std::string         fStatsStart;             // '# Start time: ...'
  std::vector<double> fStatsTimes;
  std::vector<Series> fMetrics;

  // ---------------------------------------------------------- metadata.json

  bool                fHasMetadata = false;
  std::string         fMetadata;
  bool                fHasMetadataCharge = false;
  double              fMetadataCharge = 0;
  struct Field { std::string key, value; bool isString; };
  std::vector<Field>  fRunFields;              // top-level scalars of metadata.json
  std::string         fRunStart, fRunStop;     // "Start Time", "Stop Time"

  // ------------------------------------------------------- other run files

  struct File { std::string name, text; };
  std::vector<File>   fRunFiles;

};

#endif // RUNCONTEXT_H_INCLUDED
