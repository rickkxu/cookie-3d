#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CookieNeighborhood.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

/**
 * The street around the player's house: neighbour houses (shells, no interiors), their yards, the road,
 * sidewalks, street lamps, a cul-de-sac, a park for big producers, and tree lines behind the lots.
 *
 * Everything is built from the same pieces as the player's house (the kit's walls and door, flat-colour
 * roofs and trims, the faceted trees and bushes), so it all matches. It's generated: placing the actor
 * builds it in the editor, and it rebuilds itself when play starts. Every piece is an instance, one
 * component per mesh and colour, so the whole street is a few dozen draw calls.
 *
 * Layout (cm): the player's house is at the origin facing -Y, on an 18 m lot. The street runs along X in front
 * of it; six lots line each side, 18 m apart.
 */
UCLASS()
class COOKIEGAME_API ACookieNeighborhood : public AActor
{
	GENERATED_BODY()

public:
	ACookieNeighborhood();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	// Changes which house, colours, trees and props each lot gets.
	UPROPERTY(EditAnywhere, Category = "Neighborhood")
	int32 Seed = 11;

private:
	struct FHouseDesign
	{
		// Footprint (cm) and storeys of 250 cm.
		double Width = 1000.0;
		double Depth = 750.0;
		int32 Storeys = 1;
		// Ridge along the width (side gables) or along the depth (a gable facing the street).
		bool bFrontGable = false;
		double PitchDegrees = 30.0;
		// Wall pieces per side, left to right seen from outside: width in kit pieces (1 = 250 cm) and kind
		// (P plain, W window, D door). Upper storeys use the Upper lists.
		TArray<TPair<double, TCHAR>> Front, Back, Side, UpperFront;
	};

	struct FPalette
	{
		FLinearColor Wall;
		FLinearColor Roof;
		FLinearColor Mailbox;
	};

	void Build();
	void Clear();

	UInstancedStaticMeshComponent* Batch(UStaticMesh* Mesh, UMaterialInterface* Material);
	void Add(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform);
	UMaterialInterface* Flat(const FLinearColor& Color);
	UMaterialInterface* Wall(const FLinearColor& Exterior);

	void BuildGround();
	void BuildStreet();
	void BuildLot(const FTransform& Lot, const FHouseDesign& Design, const FPalette& Palette, FRandomStream& Random);
	void BuildHouse(const FTransform& Lot, const FHouseDesign& Design, const FPalette& Palette, FRandomStream& Random);
	void BuildWallRun(const FTransform& Lot, const TArray<TPair<double, TCHAR>>& Pieces, const FVector& From, const FVector& Along,
		double Yaw, double Z, UMaterialInterface* WallMaterial, TArray<FVector>* OutWindowCentres);
	void BuildTree(const FTransform& Where, bool bPine, double Scale);
	void BuildParkAndTreeLines(FRandomStream& Random);

	static TArray<FHouseDesign> MakeDesigns();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	TMap<TPair<UStaticMesh*, UMaterialInterface*>, UInstancedStaticMeshComponent*> BatchLookup;
	TMap<uint32, UMaterialInterface*> FlatLookup;
	TMap<uint32, UMaterialInterface*> WallLookup;
};
