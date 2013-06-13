Summary
=======
This is the repository for LunaSysMgrIpc, the webOS IPC library used by luna-sysmgr.

How to Build on Linux
=====================

### Building the latest "stable" version

Clone the repository openwebos/build-desktop and follow the instructions in the README file.

### Building your local clone

First follow the directions to build the latest "stable" version.

To build your local clone of luna-sysmgr-ipc instead of the "stable" version installed with the build-webos-desktop script:  
* Open the build-webos-desktop.sh script with a text editor
* Locate the function build_luna-sysmgr-ipc
* Change the line "cd $BASE/luna-sysmgr-ipc" to use the folder containing your clone, for example "cd ~/github/luna-sysmgr-ipc"
* Close the text editor
* Remove the file ~/luna-desktop-binaries/luna-sysmgr-ipc/luna-desktop-build.stamp
* Start the build

Cautions:
* When you re-clone openwebos/build-desktop, you'll have to overwrite your changes and reapply them
* Components often advance in parallel with each other, so be prepared to keep your cloned repositories updated
* Fetch and rebase frequently

# Copyright and License Information

All content, including all source code files and documentation files in this repository except otherwise noted are: 

 Copyright (c) 2010-2013 LG Electronics, Inc.

All content, including all source code files and documentation files in this repository except otherwise noted are:
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this content except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
