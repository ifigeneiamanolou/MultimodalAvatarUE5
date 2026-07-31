#pragma once

/*
	The idea is only one sentence can be active at a time. By active we mean audio and emotion metadata from that sentence is sent to UE5.
	Dynamically, audio and emotion data are coming from the python backend and processed through the audio and emotion FIFO queues respectively.
	These queues operate on a consumer-producer basis with a sentinel value -1 used to signal the end of audio in the consumers. To avoid audio data
	lost in UE5 if the websocket does not receive a chunk as a whole we store audio data received into a buffer FPendingAudio until all bytes have
	been received, making sure that the number of bytes received is equal to the number expected. The producers place the audio and the emotion in 
	the two queues in a map indexed by sentence id in order to relate the audio chunks with the emotion metadata (since emotion parameters are
	generated in a sentence basis). The need for a map arises to handle the scenario the python backend generates audio and emotion data faster than
	this is sent to Audio2Face. Whenever an audio chunk is inserted in the map, a method is called that tries to dispatch the audio data to 
	Audio2Face, checking that the first audio chunk has actually arrived, as well as emotion metadata. Then, it sents the contents of the audio 
	buffer to Audio2Face and clears it. In case the audio for this sentence has ended, we move on to ActiveSequenceId. The following are assumed by
	the python backend:

	1) only one type of data is coming (audio, headers or emotion) using an asyncio lock
	2) only once python backend has sent all audio and emotion data (including end information) for one sentence can it start sending data for the
	next sentence using an asyncio TaskGroup

	To enable communication between the Uvicorn backend client and the UE5 application, a web socket server is rendered at port 7865. This means that in the 
	security group of the AWS Windows instance TCP port 7865 needs to be open to allow for incoming traffic from the Uvicorn client. SImilarly, in Windows 
	firewall port 7865 needs to be open in the outbound rules in the local computer used to run the Uvicorn server, as well as in the inbound rules in the
	Windows EC2 instance.
	 
*/
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


// Delegates to notify the blueprints that a client is connected / sending text
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnClientConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUserInput);

using namespace std;

// Structs
struct FEmotion {
	int sequenceId;
	int amazement;
	int anger;
	int cheeckiness;
	int disgust;
	int fear;
	int grief;
	int joy;
	int outOfBreath;
	int pain;
	int sadness;
	int neutral;
};

struct FPendingAudio {
	TArray<uint8> audio;
	int sequenceId;					// id of the sentence
	int chunkId;				    // id of the chunk
	int expected_bytes;
	bool waiting = false;
};

struct FAudioMessage {
	TArray<uint8> audio;
	int sequenceId;
	int chunkId;
};

struct FPendingSentence {
	bool firstChunkArrived = false;		// audio condition to start sending to Audio2Face
	bool audioEnded = false;			// condition to move to the next sequence
	bool emotionReady = false;			// emotion condition to start sending to Audio2Face
	bool dispatched = false;			// Indicates whether sentence has been dispatched to Audio2Face
	FEmotion emotion;					// emotion parameters
	TArray<uint8> buffer;				// buffer for incoming chunks from Orpheus3B
};

UCLASS()
class AVATARPROJECT_API UMyGameInstance : public UGameInstance, public FTickableGameObject
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintAssignable, Category = "WebSockets")
	FOnClientConnected OnClientConnected;

	UPROPERTY(BlueprintAssignable, Category = "WebSockets")
	FOnUserInput OnUserInput;

	virtual void Init() override;
	virtual void Shutdown() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
private:
	void passAudio(TArray<uint8> data, UObject* world, FEmotion emotion);
    // Replace the declaration at line 96 with the correct type
    UACEAudioCurveSourceComponent* getAudioCurveSource(UObject* world);
	void endAudio(UObject* world);
	AActor* myActor;
	void handleHeader(int seq_id, int chunk_id, int length);
	void produceEmotion(int id, map<string, float> emotions);
	void consumeAudio();
	void consumeEmotion();
	void handleAudio(const void* audio, SIZE_T bytesRemaining, SIZE_T size);
	void tryDispatch(int id);
	void handleEnd(int seq_id);
	void handleAudioEnd();

	FPendingSentence pendingSentence;						// Currently incoming sentence
	FPendingAudio pendingAudio;								// Currently incoming audio chunk
	TQueue<FAudioMessage, EQueueMode::Mpsc> audioQueue;		// Queue of audio chunk
	TQueue<FEmotion, EQueueMode::Mpsc> emotionQueue;		// Queue of emotion parameters
	map<int, FPendingSentence> myMap;						// Map of pending sentence

	// Avoid overfilling or draining too early
	mutex mEmotion;
	mutex mAudio;
	condition_variable cvEmotion;
	condition_variable cvAudio;

	int ActiveSequenceId = 0;		// currently active sentence 
	bool bUserStarted = false;      // Indicates whether the user has sent a message

	// WebSocket server
	TUniquePtr<IServerWebSocket> webServer;
};
