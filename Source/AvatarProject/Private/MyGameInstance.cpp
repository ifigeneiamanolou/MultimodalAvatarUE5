#include "MyGameInstance.h"
#include "WebSocketsModule.h"
#include "ACETypes.h"
#include "ACERuntimeModule.h"
#include "ACEAudioCurveSourceComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Optional.h" // TOptional
#include <json.hpp>
#include <map>
#define MAX_BUFF_SIZE 10

void UMyGameInstance::Init()
{
	Super::Init();

	// Ensure the web sockets module is loaded
	if (!FModuleManager::Get().IsModuleLoaded("WebSockets")) {
		FModuleManager::Get().LoadModule("WebSockets");
	}

	// Create a web socket
	const FString& URL = TEXT("ws://localhost:8765");
	const FString& PROTOCOL = TEXT("ws");						// Switch to WSS in production
	websocket->FWebSocketsModule::Get().CreateWebSocket(URL, PROTOCOL);

	// Connect to the web socket
	websocket->Connect();

	websocket->OnConnected().AddLambda([]() {
		UE_LOG(LogTemp, Display, TEXT("Successfully connected !"));

	});

	websocket->OnConnectionError().AddLambda([](const FString& Error) {
		UE_LOG(LogTemp, Warning, TEXT("Error during connection !"));
	});
	websocket->OnMessage().AddLambda([this](const FString& message) {
		UE_LOG(LogTemp, Display, TEXT("Received message"));
		if (message == "[[DONE]]") {
			UE_LOG(LogTemp, Display, TEXT("stream complete !"));
			if (ActiveSequenceId != -1) {
				endAudio(GetWorld());
				ActiveSequenceId = -1;
			};
			this->produceEmotion(-1, std::map<std::string, int>());
			this->handleHeader(-1, 0, 0);
			
			return;
		};

		try {
			json parsed = json::parse(message);
			if (parsed.contains("type") && parsed["type"].is_string()) {
				if (parsed["type"] == "emotion") {
					UE_LOG(LogTemp, Display, TEXT("Emotion received"));
					this->produceEmotion(parsed["sequence_id"].get<int>(), 
										 parsed["predictions"].get<std::map<std::string, int>>());
				}
				else if(parsed["type"] == "audio_chunk") {
					UE_LOG(LogTemp, Display, TEXT("Audion header received"));
					this->handleHeader(parsed["sentence_id"], parsed["chunk_index"], parsed["legnth"]);
				}
				else if (parsed["type"] == "audio_end") {
					UE_LOG(LogTemp, Display, TEXT("Audion for sentence ended"));
					this->handleEnd(parsed["sentence_id"]);
				}
				else {
					UE_LOG(LogTemp, Warning, TEXT("Invalid type field value !"));
				}
			} 
			else {
				UE_LOG(LogTemp, Warning, TEXT("No type field !"));
			}

		} catch (const json::parse_error& e) {
			UE_LOG(LogTemp, Warning, TEXT("Parse error of json input !"));
		} catch (const json::type_error& e) {
			UE_LOG(LogTemp, Warning, TEXT("Type error when reading json input!"));
		}
	});

	websocket->OnRawMessage().AddLambda([this](const void* data, SIZE_T Size, SIZE_T BytesRemaining) {
		UE_LOG(LogTemp, Display, TEXT("Received audio chunk"));
		this->handleAudio(data, BytesRemaining, Size);
	});

	websocket->OnClosed().AddLambda([this](int StatusCode, const FString& Reason, bool bWasClean) {
		UE_LOG(LogTemp, Warning, TEXT("Connection closed !"));
	});
};

void UMyGameInstance::handleEnd(int seq_id) {
	myMap[seq_id].audioEnded = true;
	tryDispatch(seq_id);
};

// Emotion queue producer
void UMyGameInstance::produceEmotion(int id, std::map<std::string, int> emotions) {
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
	while (true) {
		FEmotion* emotion = emotionQueue.Peek();
		emotionQueue.Pop();

		// Sentinel value
		if (emotion->sequenceId == -1) {
			break;
		};

		// Update sentence map if necessary
		if (emotion && myMap.contains(emotion->sequenceId)) {
			myMap[emotion->sequenceId].emotion = *emotion;
			myMap[emotion->sequenceId].emotionReady = true;
		}
		else {
			FPendingSentence sentence;
			sentence.emotionReady = true;
			sentence.emotion = *emotion;
			myMap.insert({ emotion->sequenceId, sentence });
		}
	}
};

void UMyGameInstance::consumeAudio() {
	while (true){
		FAudioMessage* message = audioQueue.Peek();
		audioQueue.Pop();

		// Sentinel value
		if (message->sequenceId == -1) {
			break;
		};

		// Update sentence map if necessary
		if (message && myMap.contains(message->sequenceId)) {
			myMap[message->sequenceId].buffer.Append(message->audio);
			if (message->chunkId == 0) {
				myMap[message->sequenceId].firstChunkArrived = true;
			}
		}
		else {
			FPendingSentence sentence;
			sentence.firstChunkArrived = true;
			sentence.buffer = message->audio;
			myMap.insert({ message->sequenceId, sentence });
		}


		// Try dispatching audio from the map
		tryDispatch(message->sequenceId);
	}
}

// Updates pending audio class attribute using the headers received
void UMyGameInstance::handleHeader(int seq_id, int chunk_id, int length) {
	if (!pendingAudio.waiting) {
		UE_LOG(LogTemp, Warning, TEXT("No pending header"));
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

void UMyGameInstance::tryDispatch(int id) {
	// Check the sentence is the one processed
	if (id != ActiveSequenceId) {
		return;
	};

	FPendingSentence& pendingSentence = myMap.at(id);

	// Check both emotion and audio data are ready
	if (!(pendingSentence.emotionReady && pendingSentence.firstChunkArrived)) {
		return;
	}

	passAudio(pendingSentence.buffer, GetWorld(), pendingSentence.emotion);
	pendingSentence.buffer.Reset();

	if (pendingSentence.audioEnded) {
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

	// Check for the number of bytes accumulated
	if (pendingAudio.audio.Num() != pendingAudio.expected_bytes) {
		UE_LOG(LogTemp, Warning, TEXT("Malformed audio bytes received"));
		return;
	}

	// format audio data
	FAudioMessage message;
	message.audio = pendingAudio.audio;
	message.chunkId = pendingAudio.chunkId;
	message.sequenceId = pendingAudio.sequenceId;

	// push item to the queue
	audioQueue.Enqueue(message);

	// Clean up pendingAudio
	pendingAudio.waiting = false;
	pendingAudio.audio = TArray<uint8>();
};

UACEAudioCurveSourceComponent* UMyGameInstance::getAudioCurveSource(UObject* world) {
	// Load the generated Metahuman class
	UClass* MetaClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, TEXT("/Game/MetaHumans/mh/BP_mh.BP_mh")));

	// Access all the actors of the current world of the specified class
	TArray<AActor*, FDefaultAllocator> out;
	UGameplayStatics::GetAllActorsOfClass(world, MetaClass, out);

	// Find the metahuman in the list of actors
	for (AActor* actor : out) {
		if (!actor) continue;		// Ignore if null
		myActor = actor;
	}

	// Access the audio ace component
	UACEAudioCurveSourceComponent* consumer = Cast<UACEAudioCurveSourceComponent>(myActor->FindComponentByClass<UACEAudioCurveSourceComponent>());

	return consumer;
}

void UMyGameInstance::passAudio(TArray<uint8> data, UObject* world, FEmotion emotion) {
	// Convert the array to TArray<float>
	TArray<float> floatAudio;
	floatAudio.SetNumUninitialized(audio.Num());
	for (int i = 0; i < audio.Num(); i++) {
		floatAudio[i] = audio[i] / 32768.0f;
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
	consumeAudio();
	consumeEmotion();

};

TStatId UMyGameInstance::GetStatId() const {
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMyGameInstance, STATGROUP_Tickables);
};

bool UMyGameInstance::IsTickable() const {
	// Condition to perform a repeated action
	return websocket.IsValid();
};

void UMyGameInstance::Shutdown() {
	Super::Shutdown();
	// Close the websocket
	if (websocket.IsValid()) {
		websocket->Close();
	};
};
