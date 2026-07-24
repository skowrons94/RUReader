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
| `-t, --ts-unit <unit>` | Unit of the `Timestamp` branch: `ps` (default), `ns`, `us`, `ms`, `s` or `raw`. |
| `-c, --compression <0-9>` | ROOT compression level. Lower is faster and bigger. |
| `-b, --buffer <MB>` | Read buffer size, 64 MB by default. |
| `-v, --verbose` | More output; given twice, prints every event. |
| `--ignore-fail` | Ignore the board failure flag instead of asking. |
| `--force-dual-trace` | Always create two trace branches. |
| `--ignore-psd-boards` | Do not write the events of PSD firmware boards. |

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
