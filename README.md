# Binary Ninja YSC Architecture/Lifter

#### Summary
This project is an architecture plugin for Binary Ninja, allowing you to disassemble and even decompile YSC format scripts from the game *Grand Theft Auto V*. The lifting to BNIL is not perfect due to complex instructions being hard to translate, so psuedocode is not perfect but does get the idea across. Work still has to be done to make each instruction semantically correct.

#### How to build/install
This project requires the Binary Ninja SDK to build. To install, please build with cmake and set your Binary Ninja install folder in binja.cmake.
You can also load in a natives.json file from [alloc8or's repo](https://raw.githubusercontent.com/alloc8or/gta5-nativedb-data/refs/heads/master/natives.json) by placing it in the plugin folder along with the rest of the files.

#### Screenshots
![alt text](img/binaryninja_AgYFAzbwfu.png)
![alt text](img/binaryninja_htbOHxoCIN.png)
