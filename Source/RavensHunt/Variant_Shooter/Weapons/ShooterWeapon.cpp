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
	ReserveAmmo = MaxReserveAmmo;
	bIsReloading = false;

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
	if (GetWorld())
	{
		FTimerManager& TimerManager = GetWorld()->GetTimerManager();
		TimerManager.ClearTimer(RefireTimer);
		TimerManager.ClearTimer(ReloadTimer);
	}

	Super::EndPlay(EndPlayReason);
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

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	}

	bIsReloading = false;

	SetActorHiddenInGame(true);

	if (WeaponOwner)
	{
		WeaponOwner->OnWeaponDeactivated(this);
	}
}

void AShooterWeapon::StartFiring()
{
	if (bIsReloading || CurrentBullets <= 0)
	{
		return;
	}

	bIsFiring = true;

	if (!GetWorld())
	{
		return;
	}

	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot > RefireRate)
	{
		Fire();
	}
	else if (bFullAuto)
	{
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, RefireRate - TimeSinceLastShot, false);
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
	if (!bIsFiring || bIsReloading || CurrentBullets <= 0)
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
	const FVector BaseDirection = PawnOwner->GetControlRotation().Vector();
	const float EffectiveAimVariance = bIsAiming ? AimVariance * AimVarianceReductionPercent / 100 : AimVariance;
	const FVector Direction = FMath::VRandCone(BaseDirection, FMath::DegreesToRadians(EffectiveAimVariance));
	const FVector End = Start + Direction * HitscanRange;

	FHitResult HitResult;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(PawnOwner);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, QueryParams);

	DrawDebugLine(GetWorld(), Start, bHit ? HitResult.ImpactPoint : End, FColor::Red, false, 2.0f, 0, 0.5f);


	if (!bHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("Hitscan: NOTHING HIT"));

		return;
	}

	AActor* HitActor = HitResult.GetActor();

	if (!HitActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Hitscan: HitActor is NULL"));

		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Hitscan HIT: Actor=%s | Component=%s"), *GetNameSafe(HitActor), *GetNameSafe(HitResult.GetComponent()));

	const float DamageResult = UGameplayStatics::ApplyPointDamage(HitActor, HitDamage, Direction, HitResult, PawnOwner->GetController(), this, HitDamageType);

	UE_LOG(LogTemp, Warning, TEXT("Hitscan DamageResult: %.1f"), DamageResult);

	if (ImpactEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), ImpactEffect, HitResult.ImpactPoint, HitResult.ImpactNormal.Rotation());
	}
}

void AShooterWeapon::ProcessShotEffects()
{
	CurrentBullets = FMath::Max(0, CurrentBullets - 1);

	if (WeaponOwner)
	{
		WeaponOwner->PlayFiringMontage(FiringMontage);
		WeaponOwner->AddWeaponRecoil(VerticalRecoil);
		WeaponOwner->AddWeaponHorizontalRecoil(HorizontalRecoil);
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}

	if (PawnOwner)
	{
		MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, ShotNoiseTag);
	}
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	if (!ProjectileClass || !GetWorld())
	{
		return;
	}

	const FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = PawnOwner;

	GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);
}

void AShooterWeapon::FireCooldownExpired()
{
	if (WeaponOwner)
	{
		WeaponOwner->OnSemiWeaponRefire();
	}
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	const FVector SpawnLoc = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);
	const FVector BaseDirection = (TargetLocation - SpawnLoc).GetSafeNormal();
	const float EffectiveAimVariance = bIsAiming ? AimVariance * AimVarianceReductionPercent / 100 : AimVariance;
	const FVector ShotDirection = FMath::VRandCone(BaseDirection, FMath::DegreesToRadians(EffectiveAimVariance));
	const FRotator AimRot = ShotDirection.Rotation();

	return FTransform(AimRot, SpawnLoc);
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

bool AShooterWeapon::CanReload() const
{
	return !bIsReloading
		&& CurrentBullets < MagazineSize
		&& ReserveAmmo > 0;
}

void AShooterWeapon::StartReloading()
{
	if (!CanReload() || !GetWorld())
	{
		return;
	}

	StopFiring();

	bIsReloading = true;

	GetWorld()->GetTimerManager().SetTimer(
		ReloadTimer,
		this,
		&AShooterWeapon::FinishReloading,
		ReloadDuration,
		false
	);
}

void AShooterWeapon::FinishReloading()
{
	if (!bIsReloading)
	{
		return;
	}

	bIsReloading = false;

	const int32 MissingBullets = MagazineSize - CurrentBullets;
	const int32 BulletsToLoad = FMath::Min(MissingBullets, ReserveAmmo);

	CurrentBullets += BulletsToLoad;
	ReserveAmmo -= BulletsToLoad;

	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}
}

void AShooterWeapon::InitializeADS()
{
	if (FirstPersonMesh)
	{
		DefaultFirstPersonTransform = FirstPersonMesh->GetRelativeTransform();
	}
}

void AShooterWeapon::SetAiming(bool bNewAiming)
{
	bIsAiming = bNewAiming;
}

void AShooterWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!FirstPersonMesh)
	{
		return;
	}

	const FVector TargetLocation = DefaultFirstPersonTransform.GetLocation() + (bIsAiming ? AimLocationOffset : FVector::ZeroVector);

	const FRotator TargetRotation = DefaultFirstPersonTransform.Rotator() + (bIsAiming ? AimRotationOffset : FRotator::ZeroRotator);

	const FVector NewLocation = FMath::VInterpTo(FirstPersonMesh->GetRelativeLocation(), TargetLocation, DeltaTime, AimTransitionSpeed);

	const FRotator NewRotation = FMath::RInterpTo(FirstPersonMesh->GetRelativeRotation(), TargetRotation, DeltaTime, AimTransitionSpeed);

	FirstPersonMesh->SetRelativeLocationAndRotation(NewLocation, NewRotation);
}