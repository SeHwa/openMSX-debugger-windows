# Building on Windows

## Steps
1. Clone this repository.

2. Create derived, derived\3rdparty, derived\3rdparty\src directories.

3. Download the latest Qt online installer from [here](https://download.qt.io/archive/online_installers/).

4. Install Qt version 5.12.2 in the directory derived\3rdparty\src\Qt5.12.2.<br>
(If you change some variables in the openmsx-debugger.props file, you can set it to different Qt version and directory name)

5. Run the x64 Command Prompt of Visual Studio 2019.

6. Execute the following command in the root directory.

```bat
msbuild -p:Configuration=Release;Platform=x64 build\msvc\openmsx-debugger.sln /m
```
7. ????

8. PROFIT!