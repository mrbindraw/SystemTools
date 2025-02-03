# SystemTools  
This plugin contains system functions with specific modifications or just wrappers for blueprints.  
More functions are planned to be added in the future.  

**C++/Blueprints functions:**
- LaunchURLEx(String: URL)
- ExecCommand(String: Command)

# Remarks  
LaunchURLEx(String: URL) it fix for UKismetSystemLibrary::LaunchURL(String: URL), only for Windows.  
Because function from engine UKismetSystemLibrary::LaunchURL(String: URL):   
- It can't move the browser window on top when the game window is in fullscreen, launches the browser behind the game window.  
- It can't close the browser window after closing the game, (if the browser was opened from the game)  
and the Steam client thinks the game is still running.  

# Install 
You can install manually by extracting archive (SystemTools-X.X.X-...zip) from
[Releases](https://github.com/mrbindraw/SystemTools/releases) to your project plugins folder  
or build example project (ExamplePrj-UEX.X-SystemTools-X.X.X.zip).
