#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "ACETypes.h"
#include "ACERuntimeModule.h"
#include "ACEAudioCurveSourceComponent.h"
#include "Tickable.h"
#include "IServerWebSocket.h"
#include<iostream>
#include<mutex>
#include<thread>
#include<condition_variable>
#include<map>
#include "MyGameInstance.generated.h"


// Delegates to notify the blueprints that a filler should start
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUserInput);

// Custom log message type to debug time delays
DECLARE_LOG_CATEGORY_EXTERN(LogTime, Log, All);

using namespace std;


struct FAudioMessage {
	TArray<uint8> audio;
	bool bIsLastChunk = false;
};

UCLASS()
class AVATARPROJECT_API UMyGameInstance : public UGameInstance, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// Expose the delegates to the blueprints
	UPROPERTY(BlueprintAssignable, Category = "WebSockets")
	FOnUserInput OnUserInput;

	// Reset the state of the game instance to prepare for a new session
	UFUNCTION(BlueprintCallable, Category = "Interview")
	void ResetSessionState();

	virtual void Init() override;
	virtual void Shutdown() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
private:
	void passAudio(TArray<uint8> data, UObject* world, FEmotion emotion, bool lastChunk, int id);
    UACEAudioCurveSourceComponent* getAudioCurveSource(UObject* world);
	AActor* myActor = nullptr;
	void consumeAudio();
	void handleAudioEnd();

	TQueue<FAudioMessage, EQueueMode::Mpsc> audioQueue;		// Queue of audio chunks
	bool bUserStarted = false;								// Indicates whether the user has sent a message to start measuring the time

	// Indicates when the audio starts being dispatched to Audio2Face ie first chunk has arrived to send signal for the fillers
	bool bInterviewAudioActive = false;	

	// WebSocket server
	TUniquePtr<IServerWebSocket> webServer;

	// Avoid overfilling or draining too early TO ADD IN CPP
	mutex mEmotion;
	mutex mAudio;
	condition_variable cvEmotion;
	condition_variable cvAudio;

	// Time data
	bool firstChunk = true;
	double StartTime;
};
