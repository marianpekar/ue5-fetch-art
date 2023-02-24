// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RemoteImagePlane.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class USceneComponent;

class IHttpRequest;
class IHttpResponse;

typedef TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> FHttpRequestPtr;
typedef TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> FHttpResponsePtr;

UCLASS()
class FETCHART_API ARemoteImagePlane : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ARemoteImagePlane();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	void OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void OnImageDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UPROPERTY(EditAnywhere)
	UStaticMeshComponent* PlaneComponent;
	
	UPROPERTY(EditAnywhere)
	UTextRenderComponent* TextComponent;

	USceneComponent* SceneComponent;

	UPROPERTY(EditAnywhere)
	int TextureWidth = 512;

	UPROPERTY(EditAnywhere)
	int CatalogId = 129884;

	bool TryGetStringField(const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>& JsonObject, const FString& FieldName, FString& OutString) const;
};
