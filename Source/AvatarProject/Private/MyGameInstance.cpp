#include "MyGameInstance.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "IServerWebSocket.h"
#include "INetWebSocket.h"
#include "ServerWebSocketModule.h"
#include "ACETypes.h"
#include "ACERuntimeModule.h"
#include "ACEAudioCurveSourceComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Optional.h" // TOptional
#include "Windows/AllowWindowsPlatformTypes.h"
#include "winsock2.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "sys/types.h"
#include <map>
#include "Misc/Base64.h"
#include<iostream>
#define MAX_BUFF_SIZE 10

using namespace std;

DEFINE_LOG_CATEGORY(LogTime);

void UMyGameInstance::Init()
{
	Super::Init();

	// Make sure the module is loaded
	if (!FModuleManager::Get().IsModuleLoaded("ServerWebSocket"))
	{
		FModuleManager::Get().LoadModule("ServerWebSocket");
	}

	FServerWebSocketModule* Module = FModuleManager::Get().GetModulePtr<FServerWebSocketModule>("ServerWebSocket");

	if (!Module) {
		UE_LOG(LogTemp, Warning, TEXT("Not able to load web sockets module !!"));
		return;
	}

	// Create a web socket server if not already available
	if (!(webServer)) {
		webServer = Module->CreateServer();
	};

	// Initialize the server and start listening for messages
	uint32 port = 7865;
	bool wbSuccess = webServer->Init(port, FNetWebSocketClientConnectedCallBack::CreateLambda([this](INetWebSocket* connectedSocket) {
		// Handle client connections
		sockaddr_in* client = connectedSocket->GetRemoteAddr();
		FString ipv4 = "";   // client ipv4 address
		uint16 port = 0;     // port of client
		if (client) {
			ipv4 = ANSI_TO_TCHAR(inet_ntoa(client->sin_addr));
			port = ntohs(client->sin_port);
			UE_LOG(LogTemp, Display, TEXT("Client connected with ipv4 %s and port %d !"), *ipv4, port);
		}

		// Handle incoming client messages
		FNetWebSocketPacketReceivedCallBack messageCallback;
		messageCallback.BindLambda([this](void* data, int32 size, ENetWebSocketMessageType type) {
			// Notify the blueprints once the backend controller has send the first audio chunk / emotion parameters
			if (!(bUserStarted)) {
				bUserStarted = true;
				StartTime = FPlatformTime::Seconds();
			};

			// Check if the message is related to the fillers
			string input(static_cast<const char*>(data), size);
			if (input == "[[FILLER]]") {
				OnUserInput.Broadcast();		// Notify the level blueprint to start a filler animation on the current metahuman
				return;
			};

			// Check if the message is related to the end of the stream
			if (input == "[[DONE]]") {
				UE_LOG(LogTemp, Warning, TEXT("Stream complete !"));
				double EndTime = FPlatformTime::Seconds();
				UE_LOG(LogTime, Display, TEXT("Time until all the sentences are finished is %.3f ms"), (EndTime - StartTime) * 1000.0);
				handleAudioEnd();
				return;
			};

			// If the message is not "FILLER" or "DONE" it has to be a base64 encoded audio string
			FString Base64String(UTF8_TO_CHAR(input.c_str());
			TArray<uint8> decodedAudio;

			if (!FBase64::Decode(input, decodedAudio)) {
				UE_LOG(LogTemp, Display, TEXT("Unable to decode base64 string !"));
				return;
			};

			FAudioMessage message;
			message.bIsLastChunk = false;
			message.audio = decodedAudio;
			audioQueue.EnQueue(message);
		});

		// Handle connection errors
		FNetWebSocketInfoCallBack errorCallback;
		errorCallback.BindLambda([ipv4, port]() {
			UE_LOG(LogTemp, Display, TEXT("error from client with ipv4 %s and port %d !"), *ipv4, port);
		});

		// Handle the event a client disconnects from the server
		FNetWebSocketInfoCallBack closeCallback;
		closeCallback.BindLambda([ipv4, port]() {
			UE_LOG(LogTemp, Display, TEXT("client with ipv4 %s and port %d disconnected from the server !"), *ipv4, port);
		});

		connectedSocket->SetReceiveCallBack(messageCallback);
		connectedSocket->SetErrorCallBack(errorCallback);
		connectedSocket->SetSocketClosedCallBack(closeCallback);
	}));

	if (wbSuccess) {
		UE_LOG(LogTemp, Display, TEXT("Successfully started server on port 7865 !"));
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Failed to start server on port 7865 !"));
	}
};

// Handle the end of all sentences in the current response
void UMyGameInstance::handleAudioEnd() {
	// Reset flags
	bUserStarted = false;
	bInterviewAudioActive = false;
};

void UMyGameInstance::ResetSessionState(){
	// Reset sentence/dispatch tracking
	ActiveSequenceId = 0;
	myMap.clear();

	// Reset pending audio state
	pendingAudio.waiting = false;
	pendingAudio.expected_bytes = 0;
	pendingAudio.chunkId = 0;
	pendingAudio.sequenceId = 0;

	// Reset session flags
	bUserStarted = false;
	bInterviewAudioActive = false;

	UE_LOG(LogTemp, Display, TEXT("Session state reset for new pixel streaming viewer"));
}


// Audio queue consumer
void UMyGameInstance::consumeAudio() {
	FAudioMessage message;

	while (audioQueue.Dequeue(message)) {
		// Check if this is the last audio chunk for the given LLM response
		if (message.bIsLastChunk) {
			passAudio(message.audio, GetWorld(), true);
			handleAudioEnd();
			return;
		}

		passAudio(message.audio, GetWorld(), false);		
	}
};

UACEAudioCurveSourceComponent* UMyGameInstance::getAudioCurveSource(UObject* world) {
	// Load the generated Metahuman class
	UClass* MetaClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, TEXT("/Game/MetaHumans/mh/BP_mh.BP_mh_C")));

	if (!MetaClass) {
		UE_LOG(LogTemp, Warning, TEXT("Failed to load Metahuman class!"));
		return nullptr;
	};

	// Access all the actors of the current world of the specified class
	TArray<AActor*, FDefaultAllocator> out;
	UGameplayStatics::GetAllActorsOfClass(world, MetaClass, out);

	if (out.Num() == 0) {
		UE_LOG(LogTemp, Warning, TEXT("No Metahuman actors found in the world!"));
		return nullptr;
	};

	// Find the metahuman in the list of actors
	for (AActor* actor : out) {
		if (!actor) continue;		// Ignore if null
		myActor = actor;
		break;						// Found the first metahuman		
	};

	if (!myActor) {
		UE_LOG(LogTemp, Warning, TEXT("No valid Metahuman actor found!"));
		return nullptr;
	};

	// Access the audio ace component
	UACEAudioCurveSourceComponent* consumer = Cast<UACEAudioCurveSourceComponent>(myActor->FindComponentByClass<UACEAudioCurveSourceComponent>());

	if (!consumer) {
		UE_LOG(LogTemp, Warning, TEXT("No ACEAudioCurveSourceComponent found on the Metahuman actor!"));
		return nullptr;
	}

	return consumer;
};

void UMyGameInstance::passAudio(TArray<uint8> data, UObject* world, bool lastChunk) {
	if (lastChunk) {
		UE_LOG(LogTime, Display, TEXT("Time until last chunk is dispatched to A2F is %.3f ms"), (FPlatformTime::Seconds() - StartTime) * 1000.0);
	}

	// Cast incoming data to int16 array
	const int16* Samples = reinterpret_cast<const int16*>(data.GetData());
	int32 NumSamples = data.Num() / sizeof(int16);

	// Convert the array to TArray<float>
	TArray<float> floatAudio;
	floatAudio.SetNum(NumSamples);
	for (int i = 0; i < NumSamples; i++) {
		floatAudio[i] = Samples[i] / 32768.0f;
	};

	// Access the audio ace component
	UACEAudioCurveSourceComponent* consumer = getAudioCurveSource(world);
	if (!consumer) {
		UE_LOG(LogTemp, Error, TEXT("passAudio: no audio curve source found, dropping chunk"));
		return;
	}

	// Debugging logs
	UE_LOG(LogTemp, Warning, TEXT("Passing audio to consumer with name %s"), *consumer->GetName());
	UE_LOG(LogTemp, Warning, TEXT("Audio chunk size: %d samples, with inital unprocessed array size %d"), floatAudio.Num(), data.Num());

	// Send the audio chunk to Audio2Face
	FACERuntimeModule::Get().AnimateFromAudioSamples(
		Cast<IACEAnimDataConsumer>(consumer),
		MakeArrayView(floatAudio),							// raw audio data in float format
		1,													// number of channels
		24000,												// sample rate			
		lastChunk,			 								// last audio chunk
		TOptional<FAudio2FaceEmotion>(),					// optional emotion parameters
		nullptr,											// face parameters
		FName("Default")									// A2F provider
	);  
}

// Called every frame
void UMyGameInstance::Tick(float DeltaTime) {
	// Calls the event loop responsible for accepting connections and incoming messages
	if (webServer) {
		webServer->Tick();
	};

	// Queue consumer
	consumeAudio();
};

TStatId UMyGameInstance::GetStatId() const {
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMyGameInstance, STATGROUP_Tickables);
};

bool UMyGameInstance::IsTickable() const {
	// Condition to perform a repeated action
	return webServer.IsValid();
};

void UMyGameInstance::Shutdown() {
	Super::Shutdown();
	// Close the web server
	if (webServer) {
		FServerWebSocketModule* Module = FModuleManager::Get().GetModulePtr<FServerWebSocketModule>("ServerWebSocket");
		if (Module) {
			Module->ShutdownModule();
		}
	}
}
