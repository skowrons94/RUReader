# Requirements

Only a working ROOT installation is required.

# Build / Installation

To build and install the repository:

```bash
git clone https://baltig.infn.it/LUNA_DAQ/RUReader.git
cd RUReader && mkdir build && cd build/
cmake ..
make && sudo make install
```

The binaries will be installed in ```/opt/RUReader/```. You can add the ```export PATH=/opt/RUReader/${PATH}``` line to your ```bashrc``` file to start it from 
terminal. As an alternative you can create a symlink:

```bash
ln -s /opt/RUReader/RUReader /usr/bin/RUReader
```

## Usage
It is necessary to know ID and names of the boards that were used during the acquisition. For show help for the input arguments use:

```bash
RUReader --help
```
