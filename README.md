# Descritpion

Simple C++ code to read the binaries from the LunaDAQ application based on XDAQ. The binaries consist in a initial header from XDAQ that contains the information on when the acquistion was started and the number of boards, followed by the board aggregates which structure is described in the DPP-PHA and DPP-PSD registers manuals (Section 2) of the CAEN boards.

# Requirements

ROOT and Boost libraries are required. The Boost libraries should already be available if ROOT is correctly installed.

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
Example usage:

```bash
build/RUReader -i ru_i1_0001_0000.caendat -o run1.root -d V1724 0
```

If more than one board is present, add additional ```-d V1724 0``` where the first argument is the board name and the second is the board ID.
