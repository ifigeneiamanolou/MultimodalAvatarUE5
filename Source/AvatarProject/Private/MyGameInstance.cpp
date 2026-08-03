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
#include "json.hpp"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "winsock2.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "sys/types.h"
#include <map>
#include<iostream>
#define MAX_BUFF_SIZE 10

using json = nlohmann::json;
using namespace std;

struct AudioHeaderMsg {
	string type;
	int sentence_id;
	int chunk_index;
	int length;
};


struct EmotionMsg {
	string type;
	int sentence_id;
	string sentence;
	string emotion;
	map<string, float> predictions;
	float maxProb;
};

struct AudioEndMsg {
	string type;
	int sentence_id;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AudioHeaderMsg, type, sentence_id, chunk_index, length);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EmotionMsg, type, sentence_id, sentence, emotion, predictions, maxProb);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AudioEndMsg, type, sentence_id);

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

	// Create a web socket server
	webServer = Module->CreateServer();

	// Initialize the server and start listening for messages
	uint32 port = 7865;
	bool wbSuccess = webServer->Init(port, FNetWebSocketClientConnectedCallBack::CreateLambda([this](INetWebSocket* connectedSocket) {
		// Notify blueprints a user was connected
		OnClientConnected.Broadcast();

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
		messageCallback.BindLambda([this](void* data, int32 size) {
			// Notify the blueprints
			if (!(bUserStarted)) {
				bUserStarted = true;
				OnUserInput.Broadcast();
			};

			// Check if audio bytes are expected
			if (pendingAudio.waiting) {
				this->handleAudio(data, 0, size);
				return;
			};

			// Otherwise parse incoming JSON string
			string input(static_cast<const char*>(data), size);
			if (input == "[[DONE]]") {
				UE_LOG(LogTemp, Display, TEXT("stream complete !"));
				bUserStarted = false;
				if (ActiveSequenceId != -1) {
					endAudio(GetWorld());
					ActiveSequenceId = -1;
				};
				this->produceEmotion(-1, map<string, float>());
				this->handleAudioEnd();

				return;
			};

			json parsed = json::parse(input, nullptr, /*allow_exceptions=*/false);

			// Check if json is malformed
			if (parsed.is_discarded()) {
				UE_LOG(LogTemp, Warning, TEXT("Malformed json !"));
				return;
			}

			// Extract the type of the message
			const string type = parsed.value("type", "");
			if (type == "emotion") {
				UE_LOG(LogTemp, Display, TEXT("Emotion received"));
				try {
					auto msg = parsed.get<EmotionMsg>();
					this->produceEmotion(msg.sentence_id, msg.predictions);
				}
				catch (const json::exception& e) {		// Handle missing key or type mismatch
					UE_LOG(LogTemp, Warning, TEXT("Invalid json %s !"), UTF8_TO_TCHAR(e.what()));
				};
			}
			else if (type == "audio_chunk") {
				UE_LOG(LogTemp, Display, TEXT("Audion header received"));
				try {
					auto msg = parsed.get<AudioHeaderMsg>();
					this->handleHeader(msg.sentence_id, msg.chunk_index, msg.length);
				} 
				catch (const json::exception& e) {		// Handle missing key or type mismatch
					UE_LOG(LogTemp, Warning, TEXT("Invalid json %s !"), UTF8_TO_TCHAR(e.what()));
				};
			}
			else if (type == "audio_end") {
				UE_LOG(LogTemp, Display, TEXT("Audio for sentence ended"));
				try {
					auto msg = parsed.get<AudioEndMsg>();
					this->handleEnd(msg.sentence_id);
				}
				catch (const json::exception& e) {		// Handle missing key or type mismatch
					UE_LOG(LogTemp, Warning, TEXT("Invalid json %s !"), UTF8_TO_TCHAR(e.what()));
				};
			}
			else {
				UE_LOG(LogTemp, Warning, TEXT("Invalid type field value !"));
			}
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

// Handle the end of a single sentence
void UMyGameInstance::handleEnd(int seq_id) {
	myMap[seq_id].audioEnded = true;
	tryDispatch(seq_id);
};

// Handle the end of all sentences in the current response
void UMyGameInstance::handleAudioEnd() {
	FAudioMessage message;
	message.audio = TArray<uint8>();
	message.sequenceId = -1;
	message.chunkId = 0;

	audioQueue.Enqueue(message);
	SpeechState = ESpeechState::Idle;
};

// Emotion queue producer
void UMyGameInstance::produceEmotion(int id, map<string, float> emotions) {
	// format emotion item
	FEmotion emotion = {
		id,
		emotions["amazement"],
		emotions["anger"],
		emotions["cheeckiness"],
		emotions["disgust"],
		emotions["fear"],
		emotions["grief"],
		emotions["joy"],
		emotions["outOfBreath"],
		emotions["pain"],
		emotions["sadness"],
		emotions["neutral"]
	};

	// push item to the queue
	emotionQueue.Enqueue(emotion);
};

// Emotion queue consumer
void UMyGameInstance::consumeEmotion() {
	FEmotion emotion;
	while (emotionQueue.Dequeue(emotion)) {
		// Sentinel value
		if (emotion.sequenceId == -1) {
			break;
		};

		// Update sentence map if necessary
		if (myMap.contains(emotion.sequenceId)) {
			myMap[emotion.sequenceId].emotion = emotion;
			myMap[emotion.sequenceId].emotionReady = true;
		}
		else {
			FPendingSentence sentence;
			sentence.emotionReady = true;
			sentence.emotion = emotion;
			myMap.insert({ emotion.sequenceId, sentence });
		}
	}
};

void UMyGameInstance::consumeAudio() {
	FAudioMessage message;

	while (audioQueue.Dequeue(message)) {
		// Sentinel value
		if (message.sequenceId == -1) {
			break;
		};

		// Update sentence map if necessary
		if (myMap.contains(message.sequenceId)) {
			myMap[message.sequenceId].buffer.Append(message.audio);
			if (message.chunkId == 0) {
				myMap[message.sequenceId].firstChunkArrived = true;
			}
		}
		else {
			FPendingSentence sentence;
			sentence.firstChunkArrived = true;
			sentence.buffer = message.audio;
			myMap.insert({ message.sequenceId, sentence });
		}

		// Try dispatching audio from the map
		tryDispatch(message.sequenceId);
	}
};

// Updates pending audio class attribute using the headers received
void UMyGameInstance::handleHeader(int seq_id, int chunk_id, int length) {
	if (pendingAudio.waiting) {
		UE_LOG(LogTemp, Warning, TEXT("Previous header still pending"));
		return;
	}

	pendingAudio.chunkId = chunk_id;
	pendingAudio.sequenceId = seq_id;
	pendingAudio.waiting = true;
	pendingAudio.expected_bytes = length;

	// Update pending sentence if necessary
	if (myMap.find(seq_id) == myMap.end()) {
		FPendingSentence sentence;
		myMap.insert({seq_id, sentence});
	};
};

UACEAudioCurveSourceComponent* UMyGameInstance::getAudioCurveSource(UObject* world) {
	// Load the generated Metahuman class
	UClass* MetaClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, TEXT("/Game/MetaHumans/mh/BP_mh.BP_mh")));

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

void UMyGameInstance::tryDispatch(int id) {
	// Check the sentence is the one processed
	if (id != ActiveSequenceId) {
		return;
	};

	FPendingSentence& pending = myMap.at(id);

	// Check both emotion and audio data are ready
	if (!(pending.emotionReady && pending.firstChunkArrived)) {
		return;
	}

	if (SpeechState == ESpeechState::PlayingFiller) {
		FACERuntimeModule::Get().CancelAnimationGeneration(getAudioCurveSource(GetWorld()));
	}

	SpeechState = ESpeechState::PlayingResponse;
	passAudio(pending.buffer, GetWorld(), pending.emotion);
	pending.buffer.Reset();

	if (pending.audioEnded) {
		myMap.erase(id);
		ActiveSequenceId++;
		if (myMap.contains(ActiveSequenceId)) {
			tryDispatch(ActiveSequenceId);
		};
	}
};

// Ensure that all data frames of a single chunk sent by Orpheus3B are accumulated in Audio2Face
void UMyGameInstance::handleAudio(const void* audio, SIZE_T bytesRemaining, SIZE_T size) {
	if (!pendingAudio.waiting){
		UE_LOG(LogTemp, Warning, TEXT("No pending header"));
		return;
	}

	pendingAudio.audio.Append(static_cast<const uint8*>(audio), static_cast<int32>(size));

	// Waiting for more data
	if (bytesRemaining > 0) {
		return;
	};

	// format audio data
	FAudioMessage message;
	message.audio = pendingAudio.audio;
	message.chunkId = pendingAudio.chunkId;
	message.sequenceId = pendingAudio.sequenceId;

	// push item to the queue
	UE_LOG(LogTemp, Warning, TEXT("Pushing audio to the queue"));
	audioQueue.Enqueue(message);

	// Clean up pendingAudio
	pendingAudio.waiting = false;
	pendingAudio.audio = TArray<uint8>();
};

void UMyGameInstance::passAudio(TArray<uint8> data, UObject* world, FEmotion emotion) {
	// Convert the array to TArray<float>
	TArray<float> floatAudio;
	floatAudio.SetNumUninitialized(data.Num());
	for (int i = 0; i < data.Num(); i++) {
		floatAudio[i] = data[i] / 32768.0f;
	};

	// Access the audio ace component
	UACEAudioCurveSourceComponent* consumer = getAudioCurveSource(world);

	// Send the audio chunk to Audio2Face
	FACERuntimeModule::Get().AnimateFromAudioSamples(
		Cast<IACEAnimDataConsumer>(consumer),
		MakeArrayView(floatAudio),		    // raw audio data in float format
		1,									// number of channels
		24000,								// sample rate			
		false,								// last audio chunk
		TOptional<FAudio2FaceEmotion>(),	// optional emotion parameters
		nullptr,							// face parameters
		FName("Default")					// A2F provider
	);
}

void UMyGameInstance::endAudio(UObject* world) {
	// Access the audio curve source component
	UACEAudioCurveSourceComponent* consumer = getAudioCurveSource(world);
	// Make an empty audio array
	TArray<float> audioFloat = TArray<float>();

	// Signal the end of audio to Audio2Face
	FACERuntimeModule::Get().AnimateFromAudioSamples(
		Cast<IACEAnimDataConsumer>(consumer),
		MakeArrayView(audioFloat),			// empty audio data
		1,									// number of channels
		24000,								// sample rate			
		true,								// last audio chunk
		TOptional<FAudio2FaceEmotion>(),	// optional emotion parameters
		nullptr,							// face parameters
		FName("Default")					// A2F provider
	);
}

// Called every frame
void UMyGameInstance::Tick(float DeltaTime) {
	// Calls the event loop responsible for accepting connections and incoming messages
	if (webServer) {
		webServer->Tick();
	};

	// Queue consumers
	consumeAudio();
	consumeEmotion();

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
