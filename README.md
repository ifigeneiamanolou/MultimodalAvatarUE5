# Overview

This repository contains the UE5 files used to build the Metahuman application. It contains the following:

* pixel streaming configuration
* added metahuman configured to use Audio2Face and Audio2Emotion
* additional source code in c++ to establish a web socket connection with the Ubuntu instance used to run Audio2Face and Orpheus3B so that received audio chunks from Orpheus3B can be transfered to Audio2Face

# Source code
The source code can be found under the Source/AvatarProject folder in the files:
* MyGameInstance.cpp (private folder)
* MyGameInstance.h (public folder)
The websocket runs on port 7865 of the Windows AWS EC2 instance. To generate the UE5 project files the AvatarProject.uproject file in the root directory can be used

# Git LFS
Git LFS is used to track large files, including .umap, .uasset and .dna files. These are tracked through the .gitattributes file in the root directory.

# Plugins
If one wishes to package the application or edit it within UE5 the NVIDIA ACE Plugin has to be manually included by following these steps:
1. Delete the NV_ACE_REFERENCE folder inside the Plugins folder
1. Download Audio2Face3D Unreal Engine 5.6 plugin through this page https://developer.nvidia.com/ace-for-games#section-getting-started
2. Copy the folder NV_ACE_REFERENCE inside the zip downloaded
1. Paste it under Plugins folder in the root directory