# cpdisp
Generate nice looking charts of character encodings within the terminal.
Only depends on icu (-licuuc)
### Usage
* -h : print this help.
* -w : print 2 byte table.
* -d [filename] : load custom icu data file.
* -i : require user input between pages (only if -w is enabled).
* -r [from]:[to] : display only pages associated with this range of bytes.
* -n : no format.
* -N : no format and print control character raw.
* -x [byte]:[byte]:[byte]... : prefix in hex.
* -c : print hex code and name of control characters and whitespace characters.

### Legend:
* Blue: Control Character
* Red: Invalid Character
* Green : Prefix of incomplete character
* Purple/Dark Magenta: Private Use Character
* Dark Yellow: Something I didn't expect
