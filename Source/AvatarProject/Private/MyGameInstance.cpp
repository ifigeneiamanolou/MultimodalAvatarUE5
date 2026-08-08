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
		messageCallback.BindLambda([this](void* data, int32 size, ENetWebSocketMessageType type) {
			// Notify the blueprints once the backend controller has send the first audio chunk / emotion parameters
			if (!(bUserStarted)) {
				bUserStarted = true;
				OnUserInput.Broadcast();
				StartTime = FPlatformTime::Seconds();
			};

			// Check if audio bytes are expected
			if (type == ENetWebSocketMessageType::Binary) {
				UE_LOG(LogTemp, Display, TEXT("Audio data received for sentence %d of chunk %d"), pendingAudio.sequenceId, pendingAudio.chunkId);
				this->handleAudio(data, size);
				return;
			};

			// Otherwise parse incoming JSON string
			string input(static_cast<const char*>(data), size);
			if (input == "[[DONE]]") {
				UE_LOG(LogTemp, Warning, TEXT("Stream complete !"));
				double EndTime = FPlatformTime::Seconds();
				UE_LOG(LogTime, Display, TEXT("Time until all the sentences are finished is %.3f ms"), (EndTime - StartTime) * 1000.0);
				handleAudioEnd();
				return;
			};

			json parsed = json::parse(input, nullptr, /*allow_exceptions=*/false);

			// Check if json is malformed
			if (parsed.is_discarded()) {
				UE_LOG(LogTemp, Warning, TEXT("Malformed json !"));
				return;
			}

			// Extract the type of the message
			const string typeJSON = parsed.value("type", "");
			if (typeJSON == "emotion") {
				try {
					auto msg = parsed.get<EmotionMsg>();
					UE_LOG(LogTemp, Display, TEXT("Emotion for sentence %d received"), msg.sentence_id);
					this->produceEmotion(msg.sentence_id, msg.predictions);
				}
				catch (const json::exception& e) {		// Handle missing key or type mismatch
					UE_LOG(LogTemp, Warning, TEXT("Invalid json %s !"), UTF8_TO_TCHAR(e.what()));
				};
			}
			else if (typeJSON == "audio_chunk") {
				try {
					auto msg = parsed.get<AudioHeaderMsg>();
					UE_LOG(LogTemp, Display, TEXT("Audio header for sentence %d and chunk %d received"), msg.sentence_id, msg.chunk_index);
					this->handleHeader(msg.sentence_id, msg.chunk_index, msg.length);
				}
				catch (const json::exception& e) {		// Handle missing key or type mismatch
					UE_LOG(LogTemp, Warning, TEXT("Invalid json %s !"), UTF8_TO_TCHAR(e.what()));
				};
			}
			else if (typeJSON == "audio_end") {
				try {
					auto msg = parsed.get<AudioEndMsg>();
					UE_LOG(LogTemp, Display, TEXT("Audio for sentence %d ended"), msg.sentence_id);
					UE_LOG(LogTime, Display, TEXT("Time for sentence %d data to be received : %.3f ms"), msg.sentence_id, (FPlatformTime::Seconds() - StartTime) * 1000.0);
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
	// Signal end of audio for the sentence
	myMap[seq_id].audioEnded = true;

	// Reset the pending audio
	if (pendingAudio.sequenceId == seq_id) {
		pendingAudio.chunkId = 0;
		pendingAudio.sequenceId = 0;
		pendingAudio.waiting = false;
		pendingAudio.expected_bytes = 0;
	}

	// Flush remaining audio chunks
	if (ActiveSequenceId == seq_id) {
		tryDispatch(seq_id);
	}
	else {
		UE_LOG(LogTemp, Display, TEXT("Sentence %d ended before becoming active (still on sentence %d)"), seq_id, ActiveSequenceId);
	}
};

// Handle the end of all sentences in the current response
void UMyGameInstance::handleAudioEnd() {
	// Notify blueprints audio has ended
	bUserStarted = false;
	bInterviewAudioActive = false;

	// Reset the active sequence id
	ActiveSequenceId = 0;

	// Clear the map of pending sentences
	myMap.clear();
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
	UE_LOG(LogTime, Display, TEXT("Time until emotion %d is placed into the queue is %.3f ms"), emotion.sequenceId, (FPlatformTime::Seconds() - StartTime) * 1000.0);
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
		
		// Used in case emotion comes after orpheus finishes sending all audio chunks
		if (ActiveSequenceId == emotion.sequenceId) {
			tryDispatch(emotion.sequenceId);
		}

		UE_LOG(LogTime, Display, TEXT("Time until emotion %d is consumed from the queue is %.3f ms"), emotion.sequenceId, (FPlatformTime::Seconds() - StartTime) * 1000.0);
	}
};

void UMyGameInstance::consumeAudio() {
	FAudioMessage message;

	while (audioQueue.Dequeue(message)) {
		// Sentinel value
		if (message.sequenceId == -1) {
			break;
		};

		if (message.chunkId == 0)
		{
			UE_LOG(LogTime, Display,
				TEXT("Time until first audio chunk of sentence %d is consumed from the queue: %.3f ms"),
				message.sequenceId,
				(FPlatformTime::Seconds() - StartTime) * 1000.0);
		}

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
			sentence.audioEnded = false;
			myMap.insert({ message.sequenceId, sentence });
		}

		// Try dispatching audio from the map
		if (ActiveSequenceId == message.sequenceId) {
			tryDispatch(message.sequenceId);
		};
	}
};

// Updates pending audio class attribute using the headers received
void UMyGameInstance::handleHeader(int seq_id, int chunk_id, int length) {
	pendingAudio.chunkId = chunk_id;
	pendingAudio.sequenceId = seq_id;
	pendingAudio.waiting = true;
	pendingAudio.expected_bytes = length;

	// Update map of pending sentence if necessary
	if (myMap.find(seq_id) == myMap.end()) {
		FPendingSentence sentence;
		sentence.audioEnded = false;
		myMap.insert({seq_id, sentence});
	};
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

void UMyGameInstance::tryDispatch(int id) {
	// Check the sentence is the one processed
	UE_LOG(LogTemp, Warning, TEXT("Trying to dispatch sentence %d"), id);

	if (id != ActiveSequenceId) {
		UE_LOG(LogTemp, Warning, TEXT("Different id for %d"), id);
		return;
	};

	// Check the sentence is contained in the map
	if (!myMap.contains(id)) {
		UE_LOG(LogTemp, Warning, TEXT("tryDispatch called for id %d not in map (already dispatched?)"), id);
		return;
	}

	FPendingSentence& pending = myMap.at(id);

	// Print the dispatch state for debugging
	UE_LOG(LogTemp, Warning, TEXT("Dispatch state id=%d emotion=%d first=%d bytes=%d"),
		id,
		pending.emotionReady,
		pending.firstChunkArrived,
		pending.buffer.Num()
	);

	// Check both emotion and audio data are ready
	if (!(pending.emotionReady && pending.firstChunkArrived)) {
		UE_LOG(LogTemp, Warning, TEXT("Not both ready for sentence %d"), id);
		return;
	}

	// Nothing new to send and not the final flush — nothing to do yet
	if (pending.buffer.Num() == 0 && !pending.audioEnded) {
		return;
	}

	// Dispatch audio to Audio2Face
	bInterviewAudioActive = true;
	passAudio(pending.buffer, GetWorld(), pending.emotion, pending.audioEnded, id);

	// Ensure the pending buffer is cleared after dispatching to not resend the same audio chunks
	pending.buffer.Reset();

	if (pending.audioEnded) {
		// This sentence is genuinely fully dispatched now — safe to advance and clean up
		myMap.erase(id);
		ActiveSequenceId++;
		if (myMap.contains(ActiveSequenceId)) {
			tryDispatch(ActiveSequenceId);   // catches up immediately if the next sentence already ended too
		}
	}
};

// Ensure that all data frames of a single chunk sent by Orpheus3B are accumulated in Audio2Face
void UMyGameInstance::handleAudio(const void* audio, SIZE_T size) {
	if (!pendingAudio.waiting){
		UE_LOG(LogTemp, Warning, TEXT("No pending header"));
		return;
	}

	// Temporary buffer to hold the audio data
	TArray<uint8> AudioData;
	AudioData.Append(static_cast<const uint8*>(audio),size);

	// Check for corrupted audio data
	if (AudioData.Num() != pendingAudio.expected_bytes)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Audio size mismatch. Expected %d bytes, received %d bytes."),
			pendingAudio.expected_bytes,
			AudioData.Num());
		return;
	}
	else {
		UE_LOG(LogTemp, Display,
			TEXT("Pushed audio chunk to the audio queue of size %d"),
			pendingAudio.expected_bytes
		);
	};

	// format audio data
	FAudioMessage message;
	message.audio = AudioData;
	message.chunkId = pendingAudio.chunkId;
	message.sequenceId = pendingAudio.sequenceId;

	if (message.chunkId == 0)
	{
		UE_LOG(LogTime, Display,
			TEXT("Time until first audio chunk of sentence %d is placed into the queue: %.3f ms"),
			message.sequenceId,
			(FPlatformTime::Seconds() - StartTime) * 1000.0);
	}

	// push item to the queue
	audioQueue.Enqueue(message);

	// Clean up pendingAudio
	pendingAudio.waiting = false;
	pendingAudio.expected_bytes = 0;	
};

void UMyGameInstance::passAudio(TArray<uint8> data, UObject* world, FEmotion emotion, bool lastChunk, int id) {
	if (lastChunk) {
		UE_LOG(LogTime, Display, TEXT("Time until sentence %d has dispatched the last chunk to A2F is %.3f ms"), id, (FPlatformTime::Seconds() - StartTime) * 1000.0);
		firstChunk = true;
	}

	if (firstChunk) {
		UE_LOG(LogTime, Display, TEXT("Time until sentence %d has dispatched the first chunk to A2F is %.3f ms"), id, (FPlatformTime::Seconds() - StartTime) * 1000.0);
		firstChunk = false;
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

	// Format emotion parameters for current sentence
	FAudio2FaceEmotion emotionParams;
	emotionParams.bEnableEmotionOverride = true;
	emotionParams.EmotionOverrides.Amazement = emotion.amazement;
	emotionParams.EmotionOverrides.Anger = emotion.anger;
	emotionParams.EmotionOverrides.Cheekiness = emotion.cheeckiness;
	emotionParams.EmotionOverrides.Disgust = emotion.disgust;
	emotionParams.EmotionOverrides.Fear = emotion.fear;
	emotionParams.EmotionOverrides.Grief = emotion.grief;
	emotionParams.EmotionOverrides.Joy = emotion.joy;
	emotionParams.EmotionOverrides.OutOfBreath = emotion.outOfBreath;
	emotionParams.EmotionOverrides.Pain = emotion.pain;
	emotionParams.EmotionOverrides.Sadness = emotion.sadness;

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
		TOptional<FAudio2FaceEmotion>(emotionParams),		// optional emotion parameters
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
