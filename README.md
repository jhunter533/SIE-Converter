# SIE-Converter
A custom program to utlize libsie to convert HBM .sie files
# libsie
## Install Libsie
This guide is for those using Linux primarily or those who dont wish to download SoMat InField/similar products. The zip file contains the manual for how to utilize the library and several more folders for each OS. The zip is included for archival reasons as finding it in HBM's site was very difficult.

1. Unzip the libsie-1.1.6-all.zip file
2. On Linux, untar the libsie-1.1.6.tar.gz file you now have
3. Navigate to the libsie-1.1.6 folder
4. Run:
```bash
./configure
make
```
You may need to install libraries such as apr using your package manager. I asssume you can read or google errors during the configure and make process to get a successful result.

To simplify the libraries usage I then added the library into my paths and zshrc via:
```bash
export LD_LIBRARY_PATH="/usr/local/lib:$LD_LIBRARY_PATH">>~/.zshrc
export LIBRARY_PATH="/usr/local/lib:$LIBRARY_PATH">>~/.zshrc
export C_INCLUDE_PATH="/usr/local/include:$C_INCLUDE_PATH">>~/.zshrc

source ~/.zshrc
```
## How to Use
Follow the manual you downloaded in the original zip. You can now use libsie-demo data.sie to fully see information including extracted metadata but this may take awhile to finish.

# Using the C program
For simplicity in converting sie files to csv for ML or analysis purposes use the c program I made in this repository. 

To Compile:
```bash
gcc -g sie_export.c -o sie_to_csv -lsie
```

See the usage with `sie_to_csv -h`

This program will try to sanitize filenames just in case allow several command line arguments such as a custom output directory which does not need to be precreated. The program outputs a single csv with the same header information in the first rows as InField. The key difference is Column 0 will be time.

