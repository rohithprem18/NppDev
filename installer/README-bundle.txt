NppDev - Notepad++ Dev Edition
==============================

An unofficial bundle of Notepad++ (https://notepad-plus-plus.org, GPL) set up for coding practice:

  Ctrl+Enter        Run the current .c / .cpp / .java / .py file in the Run panel
                    (Input, Expected Output, time, Accepted / Wrong answer / TLE / runtime error)
  Ctrl+P            Quick Open a file of the open folder (name:42 jumps to line 42)
  Ctrl+Shift+P      Command Palette
  Ctrl+Shift+O      Open Folder          Ctrl+Shift+R   Open a recent folder
  Ctrl+B            Show / hide the folder panel
  Ctrl+Shift+N      New Window           Ctrl+Alt+N     New File in the folder
  Ctrl+/            Toggle comment       Ctrl+D / Ctrl+Shift+L   Select next / all matches
  Alt+Up / Down     Move line            Alt+Shift+Down          Duplicate line
  Ctrl+Alt+Up/Down  Add a cursor above / below
  F12 / Shift+F12   Go to definition / find references (IntelliSense)
  F2                Rename (a symbol in code, a file in the folder panel)
  F8 / Shift+F8     Next / previous error      Alt+Shift+F   Format document

Each folder remembers its own open files, like VS Code. New .c / .cpp / .java files start
from a template (C++: #include <bits/stdc++.h>).

Tools
-----
Run and IntelliSense use Python, GCC / G++ (with clangd) and a Java JDK. NppDev checks for them
at every start and offers to install what is missing or too old (with winget and pip, after you
click Install). When you run a file whose tool is missing, it asks to install it and then runs
your program. Tools you already have are used as they are; too-old ones (GCC < 9, Python < 3.9,
JDK < 21 for Java IntelliSense) get a newer version installed next to them. Pick versions in
Plugins > Dev Tools > Check / Install Tools.

Notes
-----
- Installed for the current user in %LOCALAPPDATA%\Programs\NppDev (no administrator rights).
  Remove it from Windows Settings > Apps, or with uninstall.exe in that folder.
- Settings are kept in the NppDev folder itself, separate from any other Notepad++.
  If another Notepad++ is already running, close it before starting NppDev (Notepad++ opens
  only one window per user unless started with -multiInst).
- The setup is not code-signed, so Windows SmartScreen may say "Windows protected your PC":
  click "More info", then "Run anyway".
- The source of the Dev Tools plugin and the setup is in the "source" folder.
