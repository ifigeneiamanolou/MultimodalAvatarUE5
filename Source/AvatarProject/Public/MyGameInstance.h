#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "IWebSocket.h"
#include "MyGameInstance.generated.h"

/**
 *  Establish a web socket connection with Orpheus3B and stream incoming audio chunks to Audio2Face
 */

UCLASS()
class AVATARPROJECT_API UMyGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;
	TSharedPtr<IWebSocket> websocket;
protected:
	void passAudio(const void* data, UObject* world, SIZE_T size);
	void endAudio(UObject* world);
	AActor* myActor;
	
};
