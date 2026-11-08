// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;
class USkeletalMeshComponent;
class UAnimMontage;
class UAnimInstance;
class UDamageType;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class EShooterFireMode : uint8
{
	Projectile UMETA(DisplayName = "Projectile"),
	Hitscan UMETA(DisplayName = "Hitscan")
};

/**
 * Base class for a simple first person shooter weapon
 * Provides both first person and third person perspective meshes
 * Handles ammo and firing logic
 * Interacts with the weapon owner through the ShooterWeaponHolder interface
 */
UCLASS(abstract)
class RAVENSHUNT_API AShooterWeapon : public AActor
{
	GENERATED_BODY()

	/** First person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	/** Third person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> ThirdPersonMesh;

protected:

	/** Cast pointer to the weapon owner */
	IShooterWeaponHolder* WeaponOwner = nullptr;

	/** Type of fire used by this weapon */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	EShooterFireMode FireMode = EShooterFireMode::Projectile;

	/** Type of projectiles this weapon will shoot */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	/** Number of bullets in a magazine */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = 0, ClampMax = 100))
	int32 MagazineSize = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 MaxReserveAmmo = 90;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ammo")
	int32 ReserveAmmo = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0.01", Units = "s"))
	float ReloadDuration = 2.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ammo")
	bool bIsReloading = false;

	FTimerHandle ReloadTimer;

	/** Number of bullets in the current magazine */
	int32 CurrentBullets = 0;

	/** Animation montage to play when firing this weapon */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> FiringMontage;

	/** AnimInstance class to set for the first person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	/** AnimInstance class to set for the third person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	/** Cone half-angle for variance while aiming */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float AimVariance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|ADS",meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "%"))
	float AimVarianceReductionPercent = 75.0f;

	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = -100, ClampMax = 100))
	float VerticalRecoil = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = -100, ClampMax = 100))
	float HorizontalRecoil = 0.5f;

	/** Name of the first person muzzle socket where projectiles will spawn */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	FName MuzzleSocketName;

	/** Distance ahead of the muzzle that bullets will spawn at */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float MuzzleOffset = 10.0f;

	/** Maximum distance of a hitscan shot */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float HitscanRange = 10000.0f;

	/** If true, this weapon will automatically fire at the refire rate */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refire")
	bool bFullAuto = false;

	/** Time between shots for this weapon */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refire", meta = (ClampMin = 0, ClampMax = 5, Units = "s"))
	float RefireRate = 0.5f;

	/** Game time of last shot fired */
	float TimeOfLastShot = 0.0f;

	/** If true, the weapon is currently firing */
	bool bIsFiring = false;

	/** Timer to handle full auto refiring */
	FTimerHandle RefireTimer;

	/** Cast pawn pointer to the owner for AI perception system interactions */
	TObjectPtr<APawn> PawnOwner = nullptr;

	/** Loudness of the shot for AI perception system interactions */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = 0, ClampMax = 100))
	float ShotLoudness = 1.0f;

	/** Max range of shot AI perception noise */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float ShotNoiseRange = 3000.0f;

	/** Tag to apply to noise generated by shooting this weapon */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception")
	FName ShotNoiseTag = FName("Shot");

	UPROPERTY(EditAnywhere, Category = "Effects")
	TObjectPtr<UNiagaraSystem> MuzzleFlashEffect;

	UPROPERTY(EditAnywhere, Category = "Effects")
	TObjectPtr<UNiagaraSystem> ImpactEffect;

public:

	AShooterWeapon();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnOwnerDestroyed(AActor* DestroyedActor);

public:
	void ActivateWeapon();
	void DeactivateWeapon();
	void StartFiring();
	void StopFiring();

protected:
	virtual void Fire();
	void FireCooldownExpired();

	virtual void FireProjectile(const FVector& TargetLocation);
	void FireHitscan();

	void ProcessShotEffects();

	FTransform CalculateProjectileSpawnTransform(const FVector& TargetLocation) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float HitDamage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	TSubclassOf<UDamageType> HitDamageType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|ADS")
	FVector AimLocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|ADS")
	FRotator AimRotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|ADS", meta = (ClampMin = "0.1"))
	float AimTransitionSpeed = 12.0f;

	bool bIsAiming = false;

	FTransform DefaultFirstPersonTransform;

	void FinishReloading();

public:

	UFUNCTION(BlueprintPure, Category = "Weapon")
	USkeletalMeshComponent* GetFirstPersonMesh() const
	{
		return FirstPersonMesh;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon")
	USkeletalMeshComponent* GetThirdPersonMesh() const
	{
		return ThirdPersonMesh;
	}

	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;

	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	int32 GetMagazineSize() const
	{
		return MagazineSize;
	}

	int32 GetBulletCount() const
	{
		return CurrentBullets;
	}

	void StartReloading();

	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	bool IsReloading() const
	{
		return bIsReloading;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	int32 GetReserveAmmo() const
	{
		return ReserveAmmo;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	bool CanReload() const;

	void SetAiming(bool bNewAiming);

	void InitializeADS();

	virtual void Tick(float DeltaTime) override;
};