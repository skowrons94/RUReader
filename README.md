# Description

Simple C++ code to read the binaries from the LunaDAQ application based on XDAQ. The binaries consist in an initial header from XDAQ that contains the information on when the acquisition was started and the number of boards, followed by the board aggregates whose structure is described in the DPP-PHA and DPP-PSD registers manuals (Section 2) of the CAEN boards.

When a run is split over several files, only the first one carries the XDAQ header; the following files continue directly with the board aggregates.

# Requirements

ROOT is required. A compiler with C++17 support is needed (`std::filesystem` is used to scan a run directory).

# Build / Installation

To build and install the repository:

```bash
git clone https://github.com/skowrons94/RUReader.git
cd RUReader
mkdir build
cd build/
cmake ..
make
sudo make install
```

The binary will be installed in ```/usr/local/bin/```.

## Usage

```bash
RUReader -i ru_i1_0001_0000.caendat -o run1.root
```

The input can also be a directory, in which case every ```.caendat``` file inside
it is converted, in alphabetical order, into a single ROOT file:

```bash
RUReader -i /data/run1/ -o run1.root
```

The boards do **not** have to be listed on the command line: the XDAQ header
describes every board of the acquisition (firmware type, number of channels,
sampling period and time tag period) and the board type is derived from it. If
the header is missing, or describes no board, the conversion stops with an
error. Use `-d` only to override what the header says:

```bash
RUReader -i run1.caendat -o run1.root -d DT5725 0 -d V1730 1
```

### Options

| Option | Meaning |
| --- | --- |
| `-i, --in <path>` | Input `.caendat` file, or a directory containing them. |
| `-o, --out <path>` | Output ROOT file. |
| `-d, --dgtz <name> <id>` | Override the board type read from the header. Repeatable. |
| `-w, --waves <1 or 2> <id>` | When two waves are available for that board, select which to store. Repeatable. |  
| `-t, --ts-unit <unit>` | Unit of the `Timestamp` branch: `ps` (default), `ns`, `us`, `ms`, `s` or `raw`. |
| `-a, --algo <name>` | ROOT compression algorithm: zlib(default), lzma, lz4, zstd. |
| `-c, --compression <0-9>` | ROOT compression level. Lower is faster and bigger. |
| `-b, --buffer <MB>` | Read buffer size, 64 MB by default. |
| `-v, --verbose` | More output; given twice, prints every event. |
| `--ignore-fail` | Ignore the board failure flag instead of asking. |
| `--force-dual-trace` | Always create two trace branches. |
| `--ignore-psd-boards` | Do not write the events of PSD firmware boards. |
| `--no-run-context` | Do not store `current.txt`, `stats.csv` and `metadata.json` of the run directory (see below). |

## Output file

The output is written as `<output>.part` and renamed to `<output>` only once it
has been closed, so a file with the final name is always complete; an older
`<output>` is removed when the conversion starts. The metadata (`StartEpoch`,
`TimestampUnit`, `Boards`), the run context and the (empty) `Data` tree are
written as soon as the file is opened. The baskets of the tree are flushed every
~30 MB of compressed data (`SetAutoFlush(-30000000)`), which bounds the memory
used, and the tree is saved together with the directory of the file
(`AutoSave("SaveSelf")`) at the end of the first cluster and then at the end of
a cluster every ~100 MB written. A conversion that is killed therefore leaves a
`.part` file that ROOT opens normally, with every entry up to the last save.

A file reaching 10 GB is continued in a new one. ROOT names the parts itself
(`<output>_1.part`, `<output>_2.part`, ...); at the end they are renamed the way
ROOT names them without the temporary suffix: `run1.root`, `run1_1.root`,
`run1_2.root`... Every part carries the metadata and the run context.

## Run context

WebDAQ writes the conditions of the run in the run directory, next to the raw
data. When they are found in the directory given with `-i`, or in the directory
of the file given with `-i`, they are stored in the ROOT file, so that the file
alone says how its data was taken (each one is optional; a missing one is only
reported):

| Object | Type | From |
| --- | --- | --- |
| `RunMetadata` | `TNamed` | `metadata.json`, verbatim (the title is the JSON text) |
| `AccumulatedCharge_metadata` | `TParameter<double>` | the `"Accumulated Charge"` value of `metadata.json`, as WebDAQ stores it |
| `Run/<field>` | `TParameter<double>` or `TNamed` | every top-level field of `metadata.json` that is a number (`TParameter`) or a string (`TNamed`): run number, start and stop time, target, voltages, run type, flag, notes... Names as in the file with anything but letters and digits turned into `_` (`Run/Target_Name`, `Run/Terminal_Voltage`) |
| `RunFiles/<file name>` | `TNamed` | every other `.json`, `.cal` and `.jsonl` file of the directory, verbatim (the title is the text): each board's register dump, its energy calibration, the ROIs, the coincidence gates and histogram settings, the calibration and register changes made during the run |
| `Current/Current`, `Current/Current_<n>` | `TGraph` | `current.txt`: current [µA] against time since the start of the run [s], one graph per current column |
| `Current/AccumulatedCharge_uC[_<n>]` | `TParameter<double>` | charge in µC (µA·s) over the run, from its start to its stop (`metadata.json`): readings joined by straight lines, the first held back to the start and the last up to the stop, steps with dt <= 0 skipped. The same number WebDAQ records live and shows on its Data page |
| `Current/MeanCurrent_uA[_<n>]`, `Current/Duration_s` | `TParameter<double>` | charge / duration, and the span the charge covers |
| `Current/ChargeSpan` | `TNamed` | `run`, or `readings` when the run's start or stop time is unknown and the charge spans the first to the last reading |
| `Current/Uncovered_s` | `TParameter<double>` | seconds of the span with no current reading for more than 10 s (the monitor was silent); that time is left out of the charge, as WebDAQ does |
| `Current/StartTime` | `TNamed` | the `### Start time: ... ###` line |
| `Stats/<alias>` | `TGraph` | `stats.csv`: one graph per metric, named after its alias (letters, digits and `_` only), titled `<alias> [unit]` |
| `Stats/<alias>_source` | `TNamed` | the Graphite path of the metric |
| `Stats/StartTime` | `TNamed` | the `# Start time:` line |

Lines caught half written are dropped: a `current.txt` line that does not parse,
a `stats.csv` row shorter than the column row. Use `--no-run-context` to leave
all of this out.

## Time stamps

The time stamp written to the tree is a full 64 bit number:

* the time tag of the event is combined with the extended time stamp of the
  extra word (and with the roll-over flag when no extended time stamp is
  available);
* every reset of that counter is detected, reported while parsing and counted in
  the final statistics, and the missing high bits are added back, so that the
  time stamp keeps growing for the whole run;
* the value is then converted using the time tag period found in the XDAQ header
  (`ns per timetag`), plus the fine time stamp when the board sends one. Use
  `--ts-unit raw` to get the bare counter of the board instead.

The unit that was used, the acquisition start epoch and the board list are
stored in the output file as `TimestampUnit`, `StartEpoch` and `Boards`.

## Output tree

The tree is called `Data` and holds one entry per event:

| Branch | Type | Meaning |
| --- | --- | --- |
| `Board`, `Channel` | `UShort_t` | board id and channel number |
| `Timestamp` | `ULong64_t` | time stamp in the selected unit |
| `Energy` | `UShort_t` | PHA only |
| `EnergyShort`, `EnergyLong` | `UShort_t` | PSD only |
| `PU`, `SATU`, `LOST` | `Bool_t` | pile-up, saturation and lost event flags |
| `Flags` | `UInt_t` | PHA: the extras field of the energy word. PSD: the flag bits of the extra word |
| `Extras2` | `UInt_t` | the raw extra word |
| `fWave` / `fWave1`, `fWave2` | `TArrayS` | analog traces, when the board sends them |
| `fDigital1`, `fDigital2` (or `fDigital1_1` …) | `TArrayS` | digital probes |

The dual trace branch names are used when the board runs in dual trace mode or
when `--force-dual-trace` is given.

A board can be set up to leave the time stamp and/or the energy (charge, for
PSD) out of its events (Board Configuration 0x8000, bits 18/19), sending only
the trace. Those events are written with `Timestamp` = 0, `Energy` = 0 (or
`EnergyShort` = `EnergyLong` = 0) and `PU` = 0, and counted in the final
statistics.
