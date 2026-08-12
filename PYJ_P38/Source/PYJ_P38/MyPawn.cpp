// Fill out your copyright notice in the Description page of Project Settings.


#include "MyPawn.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MyRocket.h"

// Sets default values
AMyPawn::AMyPawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Box);

	Left = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Left"));
	Left->SetupAttachment(Body);

	Right = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Right"));
	Right->SetupAttachment(Body);

	Left->SetWorldLocation(FVector(38.0f, -20.0f, 0.0f));
	Right->SetWorldLocation(FVector(38.0f, 20.0f, 0.0f));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Box);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));

	static ConstructorHelpers::FClassFinder<AMyRocket> ClassPath(
		TEXT("/Script/Engine.Blueprint'/Game/Blueprints/BP_Rocket.BP_Rocket_C'")
	);

	if (ClassPath.Succeeded())
	{
		RocketTemplate = ClassPath.Class;
	}
}

void AMyPawn::CallBlueprint(int Money, FString Name)
{
	UE_LOG(LogTemp, Warning, TEXT("Execute CPP %d %s"), Money, *Name);
}

// Called when the game starts or when spawned
void AMyPawn::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AMyPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Left->AddLocalRotation(FRotator(0, 0, 1440.0f * DeltaTime));
	Right->AddLocalRotation(FRotator(0, 0, 1440.0f * DeltaTime));
}

// Called to bind functionality to input
void AMyPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAxis(TEXT("Pitch"), this, &AMyPawn::Pitch);
	PlayerInputComponent->BindAxis(TEXT("Roll"), this, &AMyPawn::Roll);
	PlayerInputComponent->BindAction(TEXT("Fire"), EInputEvent::IE_Pressed, this, &AMyPawn::Fire);
}

void AMyPawn::Pitch(float Value)
{

	AddActorLocalRotation(
		UGameplayStatics::GetWorldDeltaSeconds(GetWorld())
		* FRotator(Value, 0, 0) * 60);
}

void AMyPawn::Roll(float Value)
{
	AddActorLocalRotation(
		UGameplayStatics::GetWorldDeltaSeconds(GetWorld())
		* FRotator(0, 0, Value) * 60);
}

void AMyPawn::Fire()
{
	UE_LOG(LogTemp, Warning, TEXT("Fire"));

	GetWorld()->SpawnActor<AMyRocket>(
		RocketTemplate,
		Body->GetSocketTransform(TEXT("RocketSpawnPoint"))
	);
}

