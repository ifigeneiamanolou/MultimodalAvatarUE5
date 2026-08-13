# Overview

This repository contains the UE5 files used to build the Metahuman application. It contains the following:

* pixel streaming configuration
* added metahuman configured to use Audio2Face and Audio2Emotion through the NVIDIA ACE plugin
* cine camera added and pointed to the metahuman in game startup
* additional source code in c++ 

The latter handles the following:
* creates a web socket server in port 7865 using C library libwebsockets under the hood
* collects audio and emotion data send by the application in two queues and consumes them in a map of pending sentences
* once both emotion and audio for a given sentence are available they are dispatched to Audio2Face
* sends a signal to UE5 blueprints to stop all fillers once the first facial expressions are detected 

# Source code
The source code can be found under the Source/AvatarProject folder in the files:
* MyGameInstance.cpp (private folder)
* MyGameInstance.h (public folder)
To generate the UE5 project files the AvatarProject.uproject file in the root directory can be used. 

# WebSocket server
This project uses a modified version of UE5-ServerWebSocket by h2ogit, licensed under MIT to build the websocket server. This can be
found under the Plugins/WebServerModule folder and is added as a git submodule of a forked repository. One change made in the source code of the above repository
is in the ServerWebSocket/Source/Private/ServerWebSocketSubsystem.cpp file in line 32 replacing = with "this" (deprecated feature). Also, the source
code was modified to distinguish between incoming text data (JSON emotion and audio headers) and binary data (audio) using a custom enum structure.

# Git LFS
Git LFS is used to track large files, including .umap, .uasset and .dna files. These are tracked through the .gitattributes file in the root directory.

# Plugins
If one wishes to package the application or edit it within UE5 the NVIDIA ACE Plugin and the WebServerModule submodule have to be manually included by following these steps:
1. Download Audio2Face3D Unreal Engine 5.6 plugin through this page https://developer.nvidia.com/ace-for-games#section-getting-started
2. Copy the folder NV_ACE_REFERENCE inside the zip downloaded
3. Paste it under Plugins folder in the root directory
4. Run the following command to include submodules:
   ```bash
	git submodule init
   ```

# Developer Instructions
After cloning the repository and installing NVIDIA ACE, you can edit the code and test the open following these steps:
1. Ensure through VS Installer that all necessary packages are installed through this guide:
https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-for-unreal-engine?application_version=4.27
2. Ensure UE5.6 is installed with the option "Metahuman Core Creator Data" is checked
3. Open the folder cloned and right click on AvatarProject.uproject and then Generate Visual Studio project files
4. Open the .sln file generated
5. If prompted with the message "The solution contains packages with vulnerabilities" click on "manage nuget packages", locate the package "Magick-NET.Q16-HDRI-AnyCPU" and switch verson 14.7 with 14.15
6. If the above version update fails locate the files AutomationTool.cproj, Gauntlet/Gauntlet.Automation.cproj and AutomationUtils/AutomationUtils.Automatio.cproj in the folder UE5.6/Engine/Source/Programs/AutomationTool and change the tag package reference related to "Magick-NET.Q16-HDRI-AnyCPU" to use version 14.15.0. Then repeat step 5
7. Right click on 'Solution avatar project' in the solution explorer and click rebuild solution making sure the engine association entry in AvatarProject.uproject is set to 5.6
and that we are in Development mode for Win64
8. Right click on the AvatarProject folder (not the general solution) and click on Set as start up project
9. Right click again and select Debug > Start new instance (this will open uE5)
10. Once UE5 has opened, right click the green arrow to start pixel streaming making sure the signaling and the turn server are running

## Solving error related to missing "Generate project files" option
1. Locate the file UnrealVersionSelector.exe in the folder C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64
2. If not present, look into C:\Program Files\Epic Games\Launcher\Engine\Binaries\Win64 and copy it to the previous folder
3. Run the script as an administrator using the "-register" option

## Packaging the application
To package the applicatio for Windows follow these steps:
1. From the main menu in UE5 click Platforms > Windows > Package project
2. Create a shortcut to the packaged application
3. Right click on the shortcut and select properties
4. Modify the target field by adding -PixelStreamingURL=ws://127.0.0.1:8888 (optionally add -RenderOffScreen so that the screen is not blocked by the UE5 app)
5. Launch the signaling and the turn server through the NVIDIA Streaming Infrastructure repository
6. Launch the UE5 app by double clicking on the shortcut created

## Setting up the signaling server
The signaling server is part of the Epic Games Pixel Streaming Infrastructure and it allows the UE5 application to stream its content to a web browser. Also, a STUN server 
is needed to identify the public IP addresses of the signaling server and the frontend server, as well as a TURN server, in case on of the two servers is behind a NAT or a firewall.
1. Clone the repository using this link : https://github.com/EpicGames/PixelStreamingInfrastructure.git
2. Navigate to the folder SignalingWebServer/platform_scripts/cmd and run the setup.bat file
3. Once the above has finished, run the start_with_turn.bat file to start the servers

## Setting up the Audio2Face server
1.	Generate an NGC API key in the NVIDIA site and save it as an environment variable in 
   ```bash
	export NGC_API_KEY = <value>
   ```
2.	Log in NGC registry
   ```bash
	echo "$NGC_API_KEY" | docker login nvcr.io --username '$oauthtoken' --password-stdin
   ```

3.	Install the credential helper and add an execution permission  
   ```bash
	wget -O docker-credential-secretservice https://github.com/docker/docker-credential-helpers/releases/download/v0.8.0/docker-credential-secretservice-v0.8.0.linux-amd64
	chmod +x docker-credential-secretservice
	sudo mv docker-credential-secretservice /usr/local/bin/
   ```
4.	Configure ~/.docker/config.json to only contain the «auths» key with its initial value along  (the directory has to be manually created)
5.	Install libsecret-1-0
6.	Log out from docker with docker logout
7.	Log in again with docker login
5.	Install the NVIDIA Container toolkit and configure Docker (guide: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html)
6.	Make a directory to place configuration YAML files needed for the TRT engine which will be mounted to a Docker volume at /mnt/configs
   ```bash
	mkdir -p ~/.cache/audio2face-3d-configs
	export LOCAL_CONFIGS=~/.cache/audio2face-3d-configs
   ```
7.	Redirect to that directory and enter custom entries in those files (files needed are advanced_config.yaml, deployment_config.yaml and james_stylization_config.yaml
   ```bash
	cd $LOCAL_CONFIGS
	nano <file_name>.yaml
   ```
8.	Configure a local cache directory for the model with the appropriate permissions
   ```bash
	mkdir -p ~/.cache/audio2face-3d
	chmod 755 ~/.cache/audio2face-3d
	export LOCAL_NIM_CACHE=~/.cache/audio2face-3d
   ```
9.	Run the docker container with GPU support and host network access (use -p for specific port mappings) with model caching, use of our custom configurations and stopping of downloaded TRT engines
   ```bash
	docker run -it --name audio2face-3d \
	 --gpus all \
	 --network=host \
	 --entrypoint /bin/bash -w /opt/nvidia/a2f_pipeline \
	 -e NIM_SKIP_A2F_START=true \
	 -e NIM_DISABLE_MODEL_DOWNLOAD=true \
	 -e NGC_API_KEY=$NGC_API_KEY \
	 -v "$LOCAL_NIM_CACHE:/tmp/a2x" \
	 -v "$LOCAL_CONFIGS:/mnt/configs/" \
	nvcr.io/nim/nvidia/audio2face-3d:2.0
   ```
(-rm was removed to retain the container once we stop running it). 


## Starting the Audio2Face server
1. Start the container with
   ```bash
	docker start audio2face-3d
   ```
2. Enter the container shell with 
   ```bash
	docker attach audio2face-3d
   ```
3. Start the server by running 
   ```bash
	/usr/local/bin/a2f_pipeline.run_
   ```
