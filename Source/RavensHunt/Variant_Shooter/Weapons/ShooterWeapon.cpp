// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"
#include "NiagaraFunctionLibrary.h"

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);
	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner())
	{
		GetOwner()->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);
	}

	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());

	CurrentBullets = MagazineSize;

	if (!HitDamageType)
	{
		HitDamageType = UDamageType::StaticClass();
	}

	if (WeaponOwner)
	{
		WeaponOwner->AttachWeaponMeshes(this);
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
	}
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	Destroy();
}

void AShooterWeapon::ActivateWeapon()
{
	SetActorHiddenInGame(false);

	if (WeaponOwner)
	{
		WeaponOwner->OnWeaponActivated(this);
	}
}

void AShooterWeapon::DeactivateWeapon()
{
	StopFiring();

	SetActorHiddenInGame(true);

	if (WeaponOwner)
	{
		WeaponOwner->OnWeaponDeactivated(this);
	}
}

void AShooterWeapon::StartFiring()
{
	bIsFiring = true;

	if (!GetWorld())
	{
		return;
	}

	const float TimeSinceLastShot =
		GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot > RefireRate)
	{
		Fire();
	}
	else if (bFullAuto)
	{
		GetWorld()->GetTimerManager().SetTimer(
			RefireTimer,
			this,
			&AShooterWeapon::Fire,
			RefireRate - TimeSinceLastShot,
			false
		);
	}
}

void AShooterWeapon::StopFiring()
{
	bIsFiring = false;

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
	}
}

void AShooterWeapon::Fire()
{
	if (!bIsFiring || CurrentBullets <= 0)
	{
		return;
	}

	switch (FireMode)
	{
	case EShooterFireMode::Projectile:
	{
		const FVector TargetLocation = WeaponOwner
			? WeaponOwner->GetWeaponTargetLocation()
			: FVector::ZeroVector;

		if (WeaponOwner)
		{
			FireProjectile(TargetLocation);
		}

		break;
	}

	case EShooterFireMode::Hitscan:
	{
		FireHitscan();
		break;
	}

	default:
	{
		break;
	}
	}

	TimeOfLastShot = GetWorld()->GetTimeSeconds();

	ProcessShotEffects();

	if (bFullAuto)
	{
		GetWorld()->GetTimerManager().SetTimer(
			RefireTimer,
			this,
			&AShooterWeapon::Fire,
			RefireRate,
			false
		);
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimer(
			RefireTimer,
			this,
			&AShooterWeapon::FireCooldownExpired,
			RefireRate,
			false
		);
	}
}

void AShooterWeapon::FireHitscan()
{
	if (!PawnOwner)
	{
		return;
	}

	const FVector Start = PawnOwner->GetPawnViewLocation();
	const FVector Direction = PawnOwner->GetControlRotation().Vector();
	const FVector End = Start + Direction * HitscanRange;

	FHitResult HitResult;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(PawnOwner);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		Start,
		End,
		ECC_Visibility,
		QueryParams
	);

	DrawDebugLine(
		GetWorld(),
		Start,
		bHit ? HitResult.ImpactPoint : End,
		FColor::Red,
		false,
		2.0f,
		0,
		0.5f
	);

	if (WeaponOwner)
	{
		WeaponOwner->AddWeaponRecoil(VerticalRecoil);
		WeaponOwner->AddWeaponHorizontalRecoil(HorizontalRecoil);
	}

	if (!bHit)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Hitscan: NOTHING HIT")
		);

		return;
	}

	AActor* HitActor = HitResult.GetActor();

	if (!HitActor)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Hitscan: HitActor is NULL")
		);

		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT(
			"Hitscan HIT: Actor=%s | Component=%s"
		),
		*GetNameSafe(HitActor),
		*GetNameSafe(HitResult.GetComponent())
	);

	const float DamageResult = UGameplayStatics::ApplyPointDamage(
		HitActor,
		HitDamage,
		Direction,
		HitResult,
		PawnOwner->GetController(),
		this,
		HitDamageType
	);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("Hitscan DamageResult: %.1f"),
		DamageResult
	);

	if (ImpactEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			ImpactEffect,
			HitResult.ImpactPoint,
			HitResult.ImpactNormal.Rotation()
		);
	}
}

void AShooterWeapon::ProcessShotEffects()
{
	if (WeaponOwner)
	{
		WeaponOwner->PlayFiringMontage(FiringMontage);
		WeaponOwner->AddWeaponRecoil(VerticalRecoil);
		WeaponOwner->AddWeaponHorizontalRecoil(HorizontalRecoil);
		WeaponOwner->UpdateWeaponHUD(CurrentBullets - 1, MagazineSize);
	}

	--CurrentBullets;

	if (CurrentBullets <= 0)
	{
		CurrentBullets = MagazineSize;
	}

	if (PawnOwner)
	{
		MakeNoise(
			ShotLoudness,
			PawnOwner,
			PawnOwner->GetActorLocation(),
			ShotNoiseRange,
			ShotNoiseTag
		);
	}
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	if (!ProjectileClass || !GetWorld())
	{
		return;
	}

	const FTransform ProjectileTransform =
		CalculateProjectileSpawnTransform(TargetLocation);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod =
		ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = PawnOwner;

	GetWorld()->SpawnActor<AShooterProjectile>(
		ProjectileClass,
		ProjectileTransform,
		SpawnParams
	);
}

void AShooterWeapon::FireCooldownExpired()
{
	if (WeaponOwner)
	{
		WeaponOwner->OnSemiWeaponRefire();
	}
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(
	const FVector& TargetLocation) const
{
	const FVector MuzzleLoc =
		FirstPersonMesh->GetSocketLocation(MuzzleSocketName);

	const FVector SpawnLoc =
		MuzzleLoc +
		((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);

	const FRotator AimRot =
		UKismetMathLibrary::FindLookAtRotation(
			SpawnLoc,
			TargetLocation +
			(UKismetMathLibrary::RandomUnitVector() * AimVariance)
		);

	return FTransform(
		AimRot,
		SpawnLoc,
		FVector::OneVector
	);
}

const TSubclassOf<UAnimInstance>&
AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>&
AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}