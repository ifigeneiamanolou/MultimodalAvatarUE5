# Overview

This branch contains the source code used to package the UE5 app for the application in the speechToSpeechPipeline branch of the 
"MultimodalAvatar" repository centered around OpenAI audio model. It contains the following:

* pixel streaming configuration
* added metahuman configured to use Audio2Face and Audio2Emotion through the NVIDIA ACE plugin
* cine camera added and pointed to the metahuman in game startup
* additional source code in c++ 

The latter handles the following:
* creates a web socket server in port 7865 using C library libwebsockets under the hood
* collects audio data send by the central backend application in a queues and consumes them by dispatching to A2F
* sends a signal to UE5 blueprints to stop all fillers once the first facial expressions are detected 

# Source code
The source code can be found under the Source/AvatarProject folder in the files:
* MyGameInstance.cpp (private folder)
* MyGameInstance.h (public folder)
To generate the UE5 project files the AvatarProject.uproject file in the root directory can be used. 

# Developer instructions

To start using this project, the README at the main of this repository can be used.

