// Fill out your copyright notice in the Description page of Project Settings.

#include "RemoteImagePlane.h"
#include "Http.h"
#include "JsonUtilities.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"

// Sets default values
ARemoteImagePlane::ARemoteImagePlane()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = false;

    SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
    SetRootComponent(SceneComponent);

    PlaneComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaneComponent"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane"));
    if (PlaneMesh.Succeeded())
    {
        PlaneComponent->SetStaticMesh(PlaneMesh.Object);
    }
    else 
    {
        UE_LOG(LogTemp, Error, TEXT("[ARemoteImagePlane] Failed to find mesh"));
    }
    PlaneComponent->SetupAttachment(SceneComponent);

    TextComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TextRenderComponent"));
    TextComponent->SetupAttachment(SceneComponent);
}

// Called when the game starts or when spawned
void ARemoteImagePlane::BeginPlay()
{
    Super::BeginPlay();

    // Create an instance of the FHttpModule class
    FHttpModule* HttpModule = &FHttpModule::Get();

    // Create an instance of the FHttpRequest class and set its attributes
    TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
    HttpRequest->SetVerb("GET");
    FString RequestURL = FString::Format(TEXT("https://api.artic.edu/api/v1/artworks/{0}?fields=artist_display,title,image_id"), { CatalogId });
    HttpRequest->SetURL(RequestURL);
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    // Bind the callback function to the request's OnProcessRequestComplete delegate
    HttpRequest->OnProcessRequestComplete().BindUObject(this, &ARemoteImagePlane::OnResponseReceived);

    // Send the request
    HttpRequest->ProcessRequest();
}

void ARemoteImagePlane::OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[ARemoteImagePlane] Request failed"));
        return;
    }
    
    // Deserialize JSON content
    FString ResponseStr = Response->GetContentAsString();
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject))
    {
        UE_LOG(LogTemp, Error, TEXT("[ARemoteImagePlane] Failed to parse JSON content"));
        return;
    }
    
    // JSON content is parsed successfully, get inner "data" block as JsonObject
    const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>& DataObject = JsonObject->GetObjectField("data");
    if (!DataObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[ARemoteImagePlane] Invalid DataObject"));
        return;
    }

    // Get string fields from "data" block
    FString ImageId, Title, ArtistDisplay, IIIFUrl;
    if(!TryGetStringField(DataObject, "image_id", ImageId) || !TryGetStringField(DataObject, "title", Title) || !TryGetStringField(DataObject, "artist_display", ArtistDisplay))
        return;
    
    // Set the text of the TextComponent
    FString LabelText = FString::Format(TEXT("{0}\n{1}"), { Title, ArtistDisplay });

    // Replace em dash with hyphen, because the font does not have an em dash glyph
    FString EnDashChar = FString::Chr(0x2013);
    FString HyphenChar = FString::Chr(0x002D);
    LabelText = LabelText.Replace(*EnDashChar, *HyphenChar);

    TextComponent->SetText(FText::FromString(LabelText));
    
    const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>& ConfigObject = JsonObject->GetObjectField("config");
    if (!ConfigObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[ARemoteImagePlane] Invalid ConfigObject"));
        return;
    }
    
    if (!TryGetStringField(ConfigObject, "iiif_url", IIIFUrl))
        return;
    
    // Create ImageUrl from IIIFUrl and ImageId 
    FString ImageUrl = FString::Format(TEXT("{0}/{1}/full/{2},/0/default.jpg"), { IIIFUrl, ImageId, TextureWidth });
    UE_LOG(LogTemp, Log, TEXT("[ARemoteImagePlane] ImageUrl: %s"), *ImageUrl); 
            
    // Create an instance of the FHttpRequest class and FHttpModule class and set its attributes
    FHttpModule* HttpModule = &FHttpModule::Get();

    TSharedRef<IHttpRequest> GetImageRequest = FHttpModule::Get().CreateRequest();
    GetImageRequest->SetVerb("GET");
    GetImageRequest->SetURL(ImageUrl);
    GetImageRequest->OnProcessRequestComplete().BindUObject(this, &ARemoteImagePlane::OnImageDownloaded);

    // Send the request
    GetImageRequest->ProcessRequest();
}

bool ARemoteImagePlane::TryGetStringField(const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>& JsonObject, const FString& FieldName, FString& OutString) const
{
    if (JsonObject->TryGetStringField(FieldName, OutString))
    {
        UE_LOG(LogTemp, Log, TEXT("[ARemoteImagePlane] %s: %s"), *FieldName, *OutString);
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[ARemoteImagePlane] Failed to get %s"), *FieldName);
        return false;
    }
}

void ARemoteImagePlane::OnImageDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[ARemoteImagePlane] Failed get image"));
        return;
    }
    
    // Fetch material from Content
    UMaterial* MaterialToInstance = LoadObject<UMaterial>(nullptr, TEXT("Material'/Game/Materials/ExampleMaterial.ExampleMaterial'"));

    // Create a dynamic material instance from ^
    UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(MaterialToInstance, nullptr);

    // Get content from response (image data) and create an ImageWrapper
    TArray<uint8> ImageData = Response->GetContent();
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

    // Decompress image to BGRA format
    TArray<uint8> UncompressedBGRA;
    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(ImageData.GetData(), ImageData.Num()) || !ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
    { 
        UE_LOG(LogTemp, Error, TEXT("[ARemoteImagePlane] Failed to wrap image data"));
        return;
    }
         
    // Create a Texture from the decompressed image
    UTexture2D* Texture = UTexture2D::CreateTransient(ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), PF_B8G8R8A8);
    Texture->CompressionSettings = TC_Default;
    Texture->SRGB = true;
    Texture->AddToRoot();

    // Copy image data
    void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, UncompressedBGRA.GetData(), UncompressedBGRA.Num());
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
    Texture->UpdateResource();

    // Set texture parameter value in the dynamic material instance
    MaterialInstance->SetTextureParameterValue("TextureParameter", Texture);

    // Assign material
    PlaneComponent->SetMaterial(0, MaterialInstance);

    // Set Plane scale to match image aspect ratio
    float AspectRatio = (float)ImageWrapper->GetHeight() / (float)ImageWrapper->GetWidth();
    PlaneComponent->SetWorldScale3D(FVector(1.f, AspectRatio, 1.f));    
}