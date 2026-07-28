# Overview

This repository contains the UE5 files used to build the Metahuman application. It contains the following:

* pixel streaming configuration
* added metahuman configured to use Audio2Face and Audio2Emotion through the NVIDIA ACE plugin
* cine camera added and pointed to the metahuman in game startup
* additional source code in c++ 

The latter handles the following:
* creates a web socket server in port 7865 using C library libwebsockets under the hood
* collects audio and emotion data send by the application in the queues and consumes them in a map
* once both emotion and audio for a given sentence are available they are dispatched to Audio2Face

# Source code
The source code can be found under the Source/AvatarProject folder in the files:
* MyGameInstance.cpp (private folder)
* MyGameInstance.h (public folder)
To generate the UE5 project files the AvatarProject.uproject file in the root directory can be used. 

# WebSocket server
This project uses a modified version of UE5-ServerWebSocket by h2ogit, licensed under MIT to build the websocket server. This can be
found under the Plugins/WebServerModule folder. The only change made in the source code of the above repository is in the 
ServerWebSocket/Source/Private/ServerWebSocketSubsystem.cpp file in line 32 replacing = with "this" (deprecated feature).

# Git LFS
Git LFS is used to track large files, including .umap, .uasset and .dna files. These are tracked through the .gitattributes file in the root directory.

# Plugins
If one wishes to package the application or edit it within UE5 the NVIDIA ACE Plugin has to be manually included by following these steps:
1. Download Audio2Face3D Unreal Engine 5.6 plugin through this page https://developer.nvidia.com/ace-for-games#section-getting-started
2. Copy the folder NV_ACE_REFERENCE inside the zip downloaded
3. Paste it under Plugins folder in the root directory

# Developer Instructions
After cloning the repository and installing NVIDIA ACE and Unreal Engine 5.6 follow these steps:
1. Ensure through VS Installer that all necessary packages are installed through this guide:
https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-for-unreal-engine?application_version=4.27
2. Ensure UE5.6 is installed
3. Open the folder cloned using Visual Studio
5. If prompted with the message "The solution contains packages with vulnerabilities" click on "manage nuget packages", locate the package "Magick-NET.Q16-HDRI-AnyCPU" and switch verson 14.7 with 14.15
6. If the above version update fails locate the files AutomationTool.cproj, Gauntlet/Gauntlet.Automation.cproj and AutomationUtils/AutomationUtils.Automatio.cproj in the folder UE5.6/Engine/Source/Programs/AutomationTool and change the tag package reference related to "Magick-NET.Q16-HDRI-AnyCPU" to use version 14.15.0. Then repeat step 5
7. Right click on 'Solution avatar project' in the solution explorer and click rebuild solution making sure the engine association entry in AvatarProject.uproject is set to 5.6
and that we are in Development mode for Win64
8. Right click on the AvatarProject folder (not the general solution) and click on Set as start up project
9. Right click again and select Debug > Start new instance (this will open uE5)
10. From the main menu click Platforms > Windows > Package project
11. Create a shortcut to the packaged application
12. Right click on the shortcut and select properties
13. Modify the target field by adding -PixelStreamingURL=ws://127.0.0.1:8888 (optionally add -RenderOffScreen so that the screen is not blocked by the UE5 app)
14. Launch the signaling and the turn server through the NVIDIA Streaming Infrastructure repository
15. Launch the UE5 app by double clicking on the shortcut created
