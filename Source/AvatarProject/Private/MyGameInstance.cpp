// Fill out your copyright notice in the Description page of Project Settings.

#include "MyGameInstance.h"
#include "WebSocketsModule.h"
#include "ACETypes.h"
#include "ACERuntimeModule.h"
#include "ACEAudioCurveSourceComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Optional.h" // TOptional


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
	websocket = FWebSocketsModule::Get().CreateWebSocket(URL, PROTOCOL);

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
			UE_LOG(LogTemp, Display, TEXT("End of audio"));
			UObject* world = GetWorld();
			this->endAudio(world);
		}
		else {
			// Pass the emotion parameters to Audio2Face
		}
		});
	websocket->OnRawMessage().AddLambda([this](const void* data, SIZE_T Size, SIZE_T BytesRemaining) {
		UE_LOG(LogTemp, Display, TEXT("Received audio chunk"));
		UObject* world = GetWorld();
		this->passAudio(data, world, Size);
		});

	websocket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean) {
		UE_LOG(LogTemp, Warning, TEXT("Connection closed !"));
		});
};

void UMyGameInstance::passAudio(const void* data, UObject* world, SIZE_T size) {
	// Process input audio 
	TArray<uint16> audio;
	audio.Append((const uint16*)data, size);
	TArray<float> audioFloat;
	for (int i = 0; i < size; i++) {
		audioFloat[i] = (float)audio[i] / 32768.0f;
	}			// Normalize to range [-1, 1]

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

	// Send the audio chunk to Audio2Face
	FACERuntimeModule::Get().AnimateFromAudioSamples(
		Cast<IACEAnimDataConsumer>(consumer),
		MakeArrayView(audioFloat),			// raw audio data in float format
		1,									// number of channels
		24000,								// sample rate			
		false,								// last audio chunk
		TOptional<FAudio2FaceEmotion>(),	// optional emotion parameters
		nullptr,							// face parameters
		FName("Default")					// A2F provider
	);
}

void UMyGameInstance::endAudio(UObject* world) {
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

void UMyGameInstance::Shutdown() {
	Super::Shutdown();
	if (websocket->IsConnected()) {
		websocket->Close();
	};
};

