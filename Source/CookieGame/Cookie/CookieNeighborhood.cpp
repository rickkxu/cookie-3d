#include "CookieNeighborhood.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	UStaticMesh* NbLoadMesh(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}

	UMaterialInterface* NbLoadMaterial(const TCHAR* Path)
	{
		return LoadObject<UMaterialInterface>(nullptr, Path);
	}

	const TCHAR* NbCubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* NbCylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* NbRampPath = TEXT("/Game/LevelPrototyping/Meshes/SM_Ramp.SM_Ramp");
	const TCHAR* NbWallPlainPath = TEXT("/Game/LowPolyInterior/Meshes/Walls/Mesh_Walls_WallFloor2_01.Mesh_Walls_WallFloor2_01");
	const TCHAR* NbWallWindowPath = TEXT("/Game/LowPolyInterior/Meshes/Walls/Mesh_Walls_WallFloor2_02.Mesh_Walls_WallFloor2_02");
	const TCHAR* NbWallDoorPath = TEXT("/Game/LowPolyInterior/Meshes/Walls/Mesh_Walls_WallFloor2_04.Mesh_Walls_WallFloor2_04");
	const TCHAR* NbDoorPath = TEXT("/Game/LowPolyInterior/Meshes/Walls/Mesh_Walls_Door_01.Mesh_Walls_Door_01");
	const TCHAR* NbFlowerPath = TEXT("/Game/LowPolyInterior/Meshes/Flowers/Mesh_Flower_06.Mesh_Flower_06");
	const TCHAR* NbFencePath = TEXT("/Game/Cookie/House/Merged/SM_SM_Fence.SM_SM_Fence");
	const TCHAR* NbTreeRoundPath = TEXT("/Game/Cookie/Foliage/SM_Tree_Round.SM_Tree_Round");
	const TCHAR* NbTreePinePath = TEXT("/Game/Cookie/Foliage/SM_Tree_Pine.SM_Tree_Pine");
	const TCHAR* NbClumpPaths[] = {
		TEXT("/Game/Cookie/Foliage/SM_Clump_A.SM_Clump_A"),
		TEXT("/Game/Cookie/Foliage/SM_Clump_B.SM_Clump_B"),
		TEXT("/Game/Cookie/Foliage/SM_Clump_C.SM_Clump_C") };
	const TCHAR* NbGrassPath = TEXT("/Game/Cookie/House/M_Grass.M_Grass");
	const TCHAR* NbPlaceholderPath = TEXT("/Game/Cookie/M_CookiePlaceholder.M_CookiePlaceholder");
	const TCHAR* NbTwoToneWallPath = TEXT("/Game/Cookie/House/Walls/MI_Wall_Living_Ext.MI_Wall_Living_Ext");

	// The player's house's palette, for matching trims and props.
	const FLinearColor NbWhite(0.93f, 0.93f, 0.9f);
	const FLinearColor NbStone(0.32f, 0.3f, 0.27f);
	const FLinearColor NbPathGrey(0.5f, 0.48f, 0.45f);
	const FLinearColor NbDarkWood(0.25f, 0.14f, 0.07f);
	const FLinearColor NbBrick(0.42f, 0.13f, 0.07f);
	const FLinearColor NbLeaves(0.1f, 0.32f, 0.06f);
	const FLinearColor NbLeavesLight(0.2f, 0.42f, 0.08f);
	const FLinearColor NbInterior(0.1f, 0.075f, 0.06f);
	const FLinearColor NbAsphalt(0.05f, 0.05f, 0.055f);
	const FLinearColor NbConcrete(0.58f, 0.57f, 0.54f);
	const FLinearColor NbLaneWhite(0.85f, 0.85f, 0.8f);
	const FLinearColor NbSteel(0.2f, 0.2f, 0.22f);

	// Ground level outside (top of the yard grass), and where lots sit.
	constexpr double NbGroundTop = -19.0;
	constexpr double NbLotSpacing = 1800.0;
	// Street cross-section along Y: north sidewalk, road, south sidewalk.
	constexpr double NbNorthWalkY = -1200.0;
	constexpr double NbRoadY = -1650.0;
	constexpr double NbSouthWalkY = -2100.0;
	constexpr double NbRoadWestX = -12000.0;
	// The cul-de-sac's centre: its 17 m sidewalk ring stops just short of the last lots (which end at x = 6300).
	constexpr double NbRoadEastX = 8300.0;
	constexpr double NbCulDeSacRadius = 1500.0;

	FTransform NbLocal(const FTransform& Lot, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator, const FVector& Scale = FVector::OneVector)
	{
		return FTransform(Rotation, Location, Scale) * Lot;
	}
}

ACookieNeighborhood::ACookieNeighborhood()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// Static, like every piece attached to it.
	RootComponent->SetMobility(EComponentMobility::Static);
#if WITH_EDITORONLY_DATA
	// It spans the whole map; never stream it out.
	bIsSpatiallyLoaded = false;
#endif
}

void ACookieNeighborhood::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

void ACookieNeighborhood::BeginPlay()
{
	Super::BeginPlay();
	// The pieces are transient (rebuilt rather than saved), so a level loaded for play rebuilds them here.
	if (Batches.IsEmpty())
	{
		Build();
	}
}

void ACookieNeighborhood::Clear()
{
	for (UInstancedStaticMeshComponent* Component : Batches)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	Batches.Reset();
	Materials.Reset();
	BatchLookup.Reset();
	FlatLookup.Reset();
	WallLookup.Reset();
}

UInstancedStaticMeshComponent* ACookieNeighborhood::Batch(UStaticMesh* Mesh, UMaterialInterface* Material)
{
	const TPair<UStaticMesh*, UMaterialInterface*> Key(Mesh, Material);
	if (UInstancedStaticMeshComponent** Found = BatchLookup.Find(Key))
	{
		return *Found;
	}
	const FName Name = MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("Neighborhood"));
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, Name, RF_Transient);
	Component->SetupAttachment(RootComponent);
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	if (Material)
	{
		for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
		{
			Component->SetMaterial(Slot, Material);
		}
	}
	Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Component->RegisterComponent();
	Batches.Add(Component);
	BatchLookup.Add(Key, Component);
	return Component;
}

void ACookieNeighborhood::Add(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform)
{
	if (Mesh)
	{
		Batch(Mesh, Material)->AddInstance(Transform, /*bWorldSpace=*/ true);
	}
}

UMaterialInterface* ACookieNeighborhood::Flat(const FLinearColor& Color)
{
	const uint32 Key = Color.ToFColor(false).ToPackedARGB();
	if (UMaterialInterface** Found = FlatLookup.Find(Key))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(NbLoadMaterial(NbPlaceholderPath), this);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Materials.Add(Material);
	FlatLookup.Add(Key, Material);
	return Material;
}

UMaterialInterface* ACookieNeighborhood::Wall(const FLinearColor& Exterior)
{
	// The kit walls' two-tone material, painted the house colour on both sides. It picks a wall's side from the
	// component's axes, not each instance's, so on instanced walls the sides would land at random; the shells have
	// no rooms to see anyway (a dark box sits behind the windows).
	const uint32 Key = Exterior.ToFColor(false).ToPackedARGB();
	if (UMaterialInterface** Found = WallLookup.Find(Key))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(NbLoadMaterial(NbTwoToneWallPath), this);
	Material->SetVectorParameterValue(TEXT("SideA"), Exterior);
	Material->SetVectorParameterValue(TEXT("SideB"), Exterior);
	Materials.Add(Material);
	WallLookup.Add(Key, Material);
	return Material;
}

TArray<ACookieNeighborhood::FHouseDesign> ACookieNeighborhood::MakeDesigns()
{
	TArray<FHouseDesign> Designs;
	using FP = TPair<double, TCHAR>;

	// A: like the player's house, a wide bungalow with side gables.
	{
		FHouseDesign D;
		D.Width = 1000.0; D.Depth = 750.0; D.PitchDegrees = 30.0;
		D.Front = { FP(0.5, 'P'), FP(1, 'W'), FP(1, 'D'), FP(1, 'W'), FP(0.5, 'P') };
		D.Back = { FP(1, 'W'), FP(1, 'P'), FP(1, 'W'), FP(1, 'W') };
		D.Side = { FP(1, 'W'), FP(1, 'P'), FP(1, 'W') };
		Designs.Add(D);
	}
	// B: deep, with a steep gable facing the street.
	{
		FHouseDesign D;
		D.Width = 750.0; D.Depth = 1000.0; D.PitchDegrees = 40.0; D.bFrontGable = true;
		D.Front = { FP(1, 'W'), FP(1, 'D'), FP(1, 'W') };
		D.Back = { FP(1, 'W'), FP(1, 'P'), FP(1, 'W') };
		D.Side = { FP(1, 'W'), FP(1, 'P'), FP(1, 'W'), FP(1, 'W') };
		Designs.Add(D);
	}
	// C: a narrow two-storey.
	{
		FHouseDesign D;
		D.Width = 750.0; D.Depth = 750.0; D.Storeys = 2; D.PitchDegrees = 35.0;
		D.Front = { FP(1, 'W'), FP(1, 'D'), FP(1, 'W') };
		D.UpperFront = { FP(1, 'W'), FP(1, 'W'), FP(1, 'W') };
		D.Back = { FP(1, 'W'), FP(1, 'P'), FP(1, 'W') };
		D.Side = { FP(1, 'W'), FP(1, 'P'), FP(1, 'W') };
		Designs.Add(D);
	}
	// D: a long, low ranch house.
	{
		FHouseDesign D;
		D.Width = 1250.0; D.Depth = 750.0; D.PitchDegrees = 24.0;
		D.Front = { FP(1, 'W'), FP(1, 'W'), FP(1, 'D'), FP(1, 'W'), FP(1, 'W') };
		D.Back = { FP(1, 'W'), FP(1, 'P'), FP(1, 'W'), FP(1, 'P'), FP(1, 'W') };
		D.Side = { FP(1, 'W'), FP(1, 'P'), FP(1, 'W') };
		Designs.Add(D);
	}
	return Designs;
}

void ACookieNeighborhood::Build()
{
	Clear();
	FRandomStream Random(Seed);

	BuildGround();
	BuildStreet();

	const TArray<FHouseDesign> Designs = MakeDesigns();
	const FPalette Palettes[] = {
		{ FLinearColor(0.9f, 0.8f, 0.45f), FLinearColor(0.18f, 0.2f, 0.24f), FLinearColor(0.1f, 0.2f, 0.45f) },     // butter, slate
		{ FLinearColor(0.55f, 0.66f, 0.48f), FLinearColor(0.3f, 0.16f, 0.08f), FLinearColor(0.55f, 0.06f, 0.05f) },  // sage, brown
		{ FLinearColor(0.52f, 0.64f, 0.76f), FLinearColor(0.1f, 0.1f, 0.11f), FLinearColor(0.85f, 0.85f, 0.8f) },    // powder blue, charcoal
		{ FLinearColor(0.86f, 0.6f, 0.55f), FLinearColor(0.12f, 0.25f, 0.15f), FLinearColor(0.15f, 0.15f, 0.15f) },  // blush, forest
		{ FLinearColor(0.88f, 0.87f, 0.82f), FLinearColor(0.42f, 0.1f, 0.07f), FLinearColor(0.1f, 0.35f, 0.15f) },   // white, brick red
		{ FLinearColor(0.6f, 0.78f, 0.66f), FLinearColor(0.55f, 0.15f, 0.06f), FLinearColor(0.55f, 0.06f, 0.05f) },  // mint, terracotta
		{ FLinearColor(0.85f, 0.72f, 0.52f), FLinearColor(0.18f, 0.2f, 0.24f), FLinearColor(0.2f, 0.2f, 0.5f) },     // cream, slate
		{ FLinearColor(0.68f, 0.62f, 0.75f), FLinearColor(0.1f, 0.1f, 0.11f), FLinearColor(0.85f, 0.6f, 0.1f) },     // lilac, charcoal
	};

	// North side (the player's side), facing the street at -Y; the player's own lot at x = 0 is left alone.
	// South side across the road, turned to face it.
	struct FLotSpot { double X; double Y; double Yaw; };
	TArray<FLotSpot> Spots;
	for (int32 I : { -2, -1, 1, 2, 3 })
	{
		Spots.Add({ I * NbLotSpacing, 0.0, 0.0 });
	}
	for (int32 I : { -2, -1, 0, 1, 2, 3 })
	{
		// The south lots' fences line the south sidewalk (y = -2200), so their houses sit 11 m behind it.
		Spots.Add({ I * NbLotSpacing, -3300.0, 180.0 });
	}
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		const FLotSpot& Spot = Spots[Index];
		const FTransform Lot(FRotator(0.0, Spot.Yaw, 0.0), FVector(Spot.X, Spot.Y, 0.0));
		const FHouseDesign& Design = Designs[(Index * 3 + Seed) % Designs.Num()];
		const FPalette& Palette = Palettes[(Index * 5 + Seed) % UE_ARRAY_COUNT(Palettes)];
		BuildLot(Lot, Design, Palette, Random);
	}

	BuildParkAndTreeLines(Random);
}

void ACookieNeighborhood::BuildGround()
{
	// One big lawn under everything, just under the player's yard so they don't fight.
	UStaticMesh* Cube = NbLoadMesh(NbCubePath);
	Add(Cube, NbLoadMaterial(NbGrassPath), FTransform(FRotator::ZeroRotator, FVector(2000.0, -1650.0, NbGroundTop - 10.0), FVector(800.0, 600.0, 0.2)));
}

void ACookieNeighborhood::BuildStreet()
{
	UStaticMesh* Cube = NbLoadMesh(NbCubePath);
	UStaticMesh* Cylinder = NbLoadMesh(NbCylinderPath);
	UMaterialInterface* Road = Flat(NbAsphalt);
	UMaterialInterface* Walk = Flat(NbConcrete);

	// Road and sidewalks (raised like a curb), from far off west to the cul-de-sac.
	const double Length = NbRoadEastX - NbRoadWestX;
	const double MidX = (NbRoadEastX + NbRoadWestX) * 0.5;
	Add(Cube, Road, FTransform(FRotator::ZeroRotator, FVector(MidX, NbRoadY, NbGroundTop + 1.5 - 2.0), FVector(Length / 100.0, 7.0, 0.04)));
	Add(Cube, Walk, FTransform(FRotator::ZeroRotator, FVector(MidX, NbNorthWalkY, NbGroundTop + 10.0 - 5.0), FVector(Length / 100.0, 2.0, 0.1)));
	Add(Cube, Walk, FTransform(FRotator::ZeroRotator, FVector(MidX, NbSouthWalkY, NbGroundTop + 10.0 - 5.0), FVector(Length / 100.0, 2.0, 0.1)));

	// Dashed centre line.
	UMaterialInterface* Lane = Flat(NbLaneWhite);
	for (double X = NbRoadWestX + 300.0; X < NbRoadEastX - 300.0; X += 700.0)
	{
		Add(Cube, Lane, FTransform(FRotator::ZeroRotator, FVector(X, NbRoadY, NbGroundTop + 1.6), FVector(3.0, 0.14, 0.01)));
	}

	// Cul-de-sac: a round end with a sidewalk ring and a planted island in the middle.
	const FVector Bulb(NbRoadEastX, NbRoadY, 0.0);
	Add(Cylinder, Walk, FTransform(FRotator::ZeroRotator, Bulb + FVector(0.0, 0.0, NbGroundTop + 10.0 - 5.0), FVector((NbCulDeSacRadius + 200.0) / 50.0, (NbCulDeSacRadius + 200.0) / 50.0, 0.1)));
	Add(Cylinder, Road, FTransform(FRotator::ZeroRotator, Bulb + FVector(0.0, 0.0, NbGroundTop + 11.0 - 2.0), FVector(NbCulDeSacRadius / 50.0, NbCulDeSacRadius / 50.0, 0.04)));
	Add(Cylinder, Walk, FTransform(FRotator::ZeroRotator, Bulb + FVector(0.0, 0.0, NbGroundTop + 20.0 - 5.0), FVector(9.0, 9.0, 0.1)));
	Add(Cylinder, NbLoadMaterial(NbGrassPath), FTransform(FRotator::ZeroRotator, Bulb + FVector(0.0, 0.0, NbGroundTop + 22.0 - 5.0), FVector(8.4, 8.4, 0.1)));
	BuildTree(FTransform(FRotator(0.0, 40.0, 0.0), Bulb + FVector(0.0, 0.0, NbGroundTop + 22.0)), false, 1.15);

	// Street lamps on both sidewalks, between lots so they never stand in front of a gate.
	UMaterialInterface* Pole = Flat(NbSteel);
	UMaterialInterface* Glass = Flat(FLinearColor(0.95f, 0.9f, 0.7f));
	for (double X = -4500.0; X <= 6300.0; X += NbLotSpacing)
	{
		for (const double Y : { NbNorthWalkY - 70.0, NbSouthWalkY + 70.0 })
		{
			const double Facing = Y > NbRoadY ? -1.0 : 1.0;
			Add(Cylinder, Pole, FTransform(FRotator::ZeroRotator, FVector(X, Y, NbGroundTop + 230.0), FVector(0.12, 0.12, 4.6)));
			Add(Cube, Pole, FTransform(FRotator::ZeroRotator, FVector(X, Y + Facing * 60.0, NbGroundTop + 455.0), FVector(0.08, 1.3, 0.08)));
			Add(Cube, Pole, FTransform(FRotator::ZeroRotator, FVector(X, Y + Facing * 115.0, NbGroundTop + 445.0), FVector(0.4, 0.55, 0.16)));
			Add(Cube, Glass, FTransform(FRotator::ZeroRotator, FVector(X, Y + Facing * 115.0, NbGroundTop + 435.0), FVector(0.32, 0.45, 0.04)));
		}
	}
}

void ACookieNeighborhood::BuildWallRun(const FTransform& Lot, const TArray<TPair<double, TCHAR>>& Pieces, const FVector& From, const FVector& Along,
	double Yaw, double Z, UMaterialInterface* WallMaterial, TArray<FVector>* OutWindowCentres)
{
	UStaticMesh* Plain = NbLoadMesh(NbWallPlainPath);
	UStaticMesh* Window = NbLoadMesh(NbWallWindowPath);
	UStaticMesh* DoorWall = NbLoadMesh(NbWallDoorPath);
	double Offset = 0.0;
	for (const TPair<double, TCHAR>& Piece : Pieces)
	{
		const double Width = Piece.Key * 250.0;
		const FVector Centre = From + Along * (Offset + Width * 0.5) + FVector(0.0, 0.0, Z);
		UStaticMesh* Mesh = Piece.Value == 'W' ? Window : (Piece.Value == 'D' ? DoorWall : Plain);
		Add(Mesh, WallMaterial, NbLocal(Lot, Centre, FRotator(0.0, Yaw, 0.0), FVector(Piece.Key, 1.0, 1.0)));
		if (Piece.Value == 'W' && OutWindowCentres)
		{
			OutWindowCentres->Add(Centre);
		}
		Offset += Width;
	}
}

void ACookieNeighborhood::BuildHouse(const FTransform& Lot, const FHouseDesign& Design, const FPalette& Palette, FRandomStream& Random)
{
	UStaticMesh* Cube = NbLoadMesh(NbCubePath);
	UStaticMesh* Ramp = NbLoadMesh(NbRampPath);
	UStaticMesh* Door = NbLoadMesh(NbDoorPath);
	UStaticMesh* Flower = NbLoadMesh(NbFlowerPath);
	UMaterialInterface* WallMaterial = Wall(Palette.Wall);
	UMaterialInterface* Gable = Flat(Palette.Wall);
	UMaterialInterface* Roof = Flat(Palette.Roof);

	const double W = Design.Width;
	const double D = Design.Depth;
	const double HalfW = W * 0.5;
	const double HalfD = D * 0.5;
	const double WallTop = 250.0 * Design.Storeys;

	// Foundation and the dark rooms you glimpse through the windows.
	Add(Cube, Flat(NbStone), NbLocal(Lot, FVector(0.0, 0.0, -10.0), FRotator::ZeroRotator, FVector((W + 20.0) / 100.0, (D + 20.0) / 100.0, 0.18)));
	Add(Cube, Flat(NbInterior), NbLocal(Lot, FVector(0.0, 0.0, WallTop * 0.5), FRotator::ZeroRotator, FVector((W - 50.0) / 100.0, (D - 50.0) / 100.0, (WallTop - 6.0) / 100.0)));

	// Walls, storey by storey: front faces -Y (the street), as on the player's house.
	TArray<FVector> FrontWindows;
	for (int32 Storey = 0; Storey < Design.Storeys; ++Storey)
	{
		const double Z = 250.0 * Storey;
		const TArray<TPair<double, TCHAR>>& Front = Storey > 0 && !Design.UpperFront.IsEmpty() ? Design.UpperFront : Design.Front;
		BuildWallRun(Lot, Front, FVector(-HalfW, -HalfD, 0.0), FVector(1.0, 0.0, 0.0), 0.0, Z, WallMaterial, Storey == 0 ? &FrontWindows : nullptr);
		BuildWallRun(Lot, Design.Back, FVector(HalfW, HalfD, 0.0), FVector(-1.0, 0.0, 0.0), 180.0, Z, WallMaterial, nullptr);
		BuildWallRun(Lot, Design.Side, FVector(HalfW, -HalfD, 0.0), FVector(0.0, 1.0, 0.0), 90.0, Z, WallMaterial, nullptr);
		BuildWallRun(Lot, Design.Side, FVector(-HalfW, HalfD, 0.0), FVector(0.0, -1.0, 0.0), -90.0, Z, WallMaterial, nullptr);
		if (Storey > 0)
		{
			// A white band between storeys.
			Add(Cube, Flat(NbWhite), NbLocal(Lot, FVector(0.0, 0.0, Z), FRotator::ZeroRotator, FVector((W + 28.0) / 100.0, (D + 28.0) / 100.0, 0.1)));
		}
	}

	// Front door (closed), step, little awning and its brackets, like the player's porch.
	double DoorX = 0.0;
	{
		double Offset = -HalfW;
		for (const TPair<double, TCHAR>& Piece : Design.Front)
		{
			if (Piece.Value == 'D')
			{
				DoorX = Offset + Piece.Key * 125.0;
			}
			Offset += Piece.Key * 250.0;
		}
	}
	Add(Door, nullptr, NbLocal(Lot, FVector(DoorX + 50.0, -HalfD - 12.0, 0.0)));
	Add(Cube, Flat(NbStone), NbLocal(Lot, FVector(DoorX, -HalfD - 47.0, -9.0), FRotator::ZeroRotator, FVector(1.8, 0.7, 0.175)));
	Add(Cube, Roof, NbLocal(Lot, FVector(DoorX, -HalfD - 50.0, 232.0), FRotator(0.0, 0.0, -12.0), FVector(1.7, 0.8, 0.06)));
	for (const double Side : { -75.0, 75.0 })
	{
		Add(Cube, Flat(NbWhite), NbLocal(Lot, FVector(DoorX + Side, -HalfD - 30.0, 214.0), FRotator::ZeroRotator, FVector(0.06, 0.4, 0.06)));
	}

	// Window boxes with flowers under some of the front windows.
	for (const FVector& Centre : FrontWindows)
	{
		if (Random.FRand() < 0.35)
		{
			continue;
		}
		Add(Cube, Flat(NbDarkWood), NbLocal(Lot, FVector(Centre.X, -HalfD - 23.0, 92.0), FRotator::ZeroRotator, FVector(1.1, 0.22, 0.18)));
		for (int32 F = 0; F < 3; ++F)
		{
			Add(Flower, nullptr, NbLocal(Lot, FVector(Centre.X - 33.0 + 33.0 * F, -HalfD - 23.0, 101.0), FRotator(0.0, -40.0 + 40.0 * F, 0.0), FVector(0.55)));
		}
	}

	// Roof: 15 cm slabs from overhanging eaves to the ridge, a diamond ridge cap, and gable ends in the wall colour
	// (the same construction as the player's roof).
	const double Pitch = FMath::DegreesToRadians(Design.PitchDegrees);
	const double Overhang = 60.0;
	const double Half = Design.bFrontGable ? HalfW : HalfD;   // the span each slope covers
	const double Run = Design.bFrontGable ? D : W;            // the ridge's length
	const double Rise = Half * FMath::Tan(Pitch);
	const double Ridge = WallTop + Rise;
	const double Eave = WallTop - Overhang * FMath::Tan(Pitch);
	const double SlopeLength = (Half + Overhang) / FMath::Cos(Pitch);
	const double SlopeMidZ = (Eave + Ridge) * 0.5 + 7.5 * FMath::Cos(Pitch);
	const double SlopeMid = (Half + Overhang) * 0.5;
	const double PitchDeg = Design.PitchDegrees;
	if (!Design.bFrontGable)
	{
		Add(Cube, Roof, NbLocal(Lot, FVector(0.0, -SlopeMid, SlopeMidZ), FRotator(0.0, 0.0, -PitchDeg), FVector((Run + 2.0 * Overhang) / 100.0, SlopeLength / 100.0, 0.15)));
		Add(Cube, Roof, NbLocal(Lot, FVector(0.0, SlopeMid, SlopeMidZ), FRotator(0.0, 0.0, PitchDeg), FVector((Run + 2.0 * Overhang) / 100.0, SlopeLength / 100.0, 0.15)));
		Add(Cube, Roof, NbLocal(Lot, FVector(0.0, 0.0, Ridge + 15.0), FRotator(0.0, 0.0, 45.0), FVector((Run + 2.0 * Overhang + 10.0) / 100.0, 0.25, 0.25)));
		const FVector GableScale(0.23, HalfD / 100.0, Rise / 100.0);
		Add(Ramp, Gable, NbLocal(Lot, FVector(-HalfW - 12.0, 0.0, WallTop), FRotator(0.0, 0.0, 0.0), GableScale));
		Add(Ramp, Gable, NbLocal(Lot, FVector(-HalfW + 12.0, 0.0, WallTop), FRotator(0.0, 180.0, 0.0), GableScale));
		Add(Ramp, Gable, NbLocal(Lot, FVector(HalfW - 12.0, 0.0, WallTop), FRotator(0.0, 0.0, 0.0), GableScale));
		Add(Ramp, Gable, NbLocal(Lot, FVector(HalfW + 12.0, 0.0, WallTop), FRotator(0.0, 180.0, 0.0), GableScale));
	}
	else
	{
		Add(Cube, Roof, NbLocal(Lot, FVector(-SlopeMid, 0.0, SlopeMidZ), FRotator(PitchDeg, 0.0, 0.0), FVector(SlopeLength / 100.0, (Run + 2.0 * Overhang) / 100.0, 0.15)));
		Add(Cube, Roof, NbLocal(Lot, FVector(SlopeMid, 0.0, SlopeMidZ), FRotator(-PitchDeg, 0.0, 0.0), FVector(SlopeLength / 100.0, (Run + 2.0 * Overhang) / 100.0, 0.15)));
		Add(Cube, Roof, NbLocal(Lot, FVector(0.0, 0.0, Ridge + 15.0), FRotator(45.0, 0.0, 0.0), FVector(0.25, (Run + 2.0 * Overhang + 10.0) / 100.0, 0.25)));
		const FVector GableScale(0.23, HalfW / 100.0, Rise / 100.0);
		Add(Ramp, Gable, NbLocal(Lot, FVector(0.0, -HalfD - 12.0, WallTop), FRotator(0.0, 90.0, 0.0), GableScale));
		Add(Ramp, Gable, NbLocal(Lot, FVector(0.0, -HalfD + 11.0, WallTop), FRotator(0.0, -90.0, 0.0), GableScale));
		Add(Ramp, Gable, NbLocal(Lot, FVector(0.0, HalfD - 11.0, WallTop), FRotator(0.0, 90.0, 0.0), GableScale));
		Add(Ramp, Gable, NbLocal(Lot, FVector(0.0, HalfD + 12.0, WallTop), FRotator(0.0, -90.0, 0.0), GableScale));
	}

	// NbBrick chimney through one slope.
	if (Random.FRand() < 0.8)
	{
		const FVector At = Design.bFrontGable ? FVector(HalfW * 0.4, HalfD * 0.4, 0.0) : FVector(-HalfW * 0.6, HalfD * 0.3, 0.0);
		const double RoofAt = Ridge - (Design.bFrontGable ? FMath::Abs(At.X) : FMath::Abs(At.Y)) * FMath::Tan(Pitch);
		const double Top = Ridge + 90.0;
		const double Bottom = RoofAt - 40.0;
		Add(Cube, Flat(NbBrick), NbLocal(Lot, FVector(At.X, At.Y, (Top + Bottom) * 0.5), FRotator::ZeroRotator, FVector(0.7, 0.7, (Top - Bottom) / 100.0)));
		Add(Cube, Flat(NbStone), NbLocal(Lot, FVector(At.X, At.Y, Top + 5.0), FRotator::ZeroRotator, FVector(0.85, 0.85, 0.1)));
	}
}

void ACookieNeighborhood::BuildTree(const FTransform& Where, bool bPine, double Scale)
{
	Add(NbLoadMesh(bPine ? NbTreePinePath : NbTreeRoundPath), nullptr, FTransform(Where.GetRotation(), Where.GetLocation(), FVector(Scale)));
}

void ACookieNeighborhood::BuildLot(const FTransform& Lot, const FHouseDesign& Design, const FPalette& Palette, FRandomStream& Random)
{
	UStaticMesh* Cube = NbLoadMesh(NbCubePath);
	BuildHouse(Lot, Design, Palette, Random);

	// Path from the porch to the gate.
	const double PorchY = -Design.Depth * 0.5 - 80.0;
	const double GateY = -1100.0;
	Add(Cube, Flat(NbPathGrey), NbLocal(Lot, FVector(0.0, (PorchY + GateY) * 0.5, -18.0), FRotator::ZeroRotator, FVector(1.5, (PorchY - GateY) / 100.0, 0.04)));

	// A picket fence like the player's, or a low hedge along the front.
	if (Random.FRand() < 0.6)
	{
		Add(NbLoadMesh(NbFencePath), nullptr, NbLocal(Lot, FVector(-900.0, -1100.0, 32.0)));
	}
	else
	{
		for (double X = -860.0; X <= 860.0; X += 105.0)
		{
			if (FMath::Abs(X) < 120.0)
			{
				continue;   // the gap for the path
			}
			const int32 Variant = Random.RandRange(0, 2);
			Add(NbLoadMesh(NbClumpPaths[Variant]), Flat(Random.FRand() < 0.5 ? NbLeaves : NbLeavesLight),
				NbLocal(Lot, FVector(X, -1060.0, -8.0), FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0), FVector(1.25, 1.0, 0.85)));
		}
	}

	// Mailbox by the gate.
	Add(Cube, Flat(Palette.Mailbox), NbLocal(Lot, FVector(150.0, -1080.0, 100.0), FRotator::ZeroRotator, FVector(0.25, 0.45, 0.22)));
	Add(Cube, Flat(NbDarkWood), NbLocal(Lot, FVector(150.0, -1080.0, 37.0), FRotator::ZeroRotator, FVector(0.08, 0.08, 1.1)));

	// Bushes along the front of the house.
	const double FrontY = -Design.Depth * 0.5 - 70.0;
	for (const double Side : { -1.0, 1.0 })
	{
		for (const double Inset : { 80.0, 260.0 })
		{
			const double X = Side * (Design.Width * 0.5 - Inset);
			if (FMath::Abs(X) < 140.0)
			{
				continue;
			}
			const int32 Variant = Random.RandRange(0, 2);
			Add(NbLoadMesh(NbClumpPaths[Variant]), Flat(Random.FRand() < 0.5 ? NbLeaves : NbLeavesLight),
				NbLocal(Lot, FVector(X, FrontY, -6.0), FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0), FVector(1.1, 0.95, 0.8)));
		}
	}

	// Two to four trees in the usual spots: front corners, back corners, the side yard.
	const FVector TreeSpots[] = { FVector(-620.0, -820.0, 0.0), FVector(660.0, -880.0, 0.0), FVector(-640.0, 520.0, 0.0),
		FVector(650.0, 540.0, 0.0), FVector(780.0, -150.0, 0.0), FVector(-780.0, -100.0, 0.0) };
	for (const FVector& Spot : TreeSpots)
	{
		if (Random.FRand() < 0.5)
		{
			continue;
		}
		const FVector Jitter(Random.FRandRange(-60.0, 60.0), Random.FRandRange(-60.0, 60.0), NbGroundTop + 1.0);
		const FTransform Where = NbLocal(Lot, Spot + Jitter, FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0));
		BuildTree(Where, Random.FRand() < 0.35, Random.FRandRange(0.85, 1.12));
	}
}

void ACookieNeighborhood::BuildParkAndTreeLines(FRandomStream& Random)
{
	// Tree lines behind both rows of lots, so the street has an edge.
	for (double X = -6000.0; X <= 7000.0; X += Random.FRandRange(380.0, 560.0))
	{
		BuildTree(FTransform(FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0), FVector(X, Random.FRandRange(1050.0, 1500.0), NbGroundTop)), Random.FRand() < 0.45, Random.FRandRange(0.9, 1.25));
		BuildTree(FTransform(FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0), FVector(X + 150.0, Random.FRandRange(-4750.0, -4300.0), NbGroundTop)), Random.FRand() < 0.45, Random.FRandRange(0.9, 1.25));
	}

	// Along the road west, where it runs off into the distance.
	for (double X = -6300.0; X > NbRoadWestX + 500.0; X -= Random.FRandRange(500.0, 900.0))
	{
		for (const double Y : { -950.0, -2350.0 })
		{
			if (Random.FRand() < 0.8)
			{
				BuildTree(FTransform(FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0), FVector(X, Y + Random.FRandRange(-250.0, 250.0), NbGroundTop)), Random.FRand() < 0.5, Random.FRandRange(0.9, 1.3));
			}
		}
	}

	// The park past the cul-de-sac: open lawn for factories and mountains, with trees round its edge.
	const FVector2D ParkMin(10500.0, -6500.0);
	const FVector2D ParkMax(19000.0, 3500.0);
	for (int32 I = 0; I < 46; ++I)
	{
		const double T = I / 46.0;
		FVector2D P;
		// Walk the edge of the park.
		const double Perimeter = 2.0 * ((ParkMax.X - ParkMin.X) + (ParkMax.Y - ParkMin.Y));
		double D = T * Perimeter;
		const double WX = ParkMax.X - ParkMin.X;
		const double WY = ParkMax.Y - ParkMin.Y;
		if (D < WX) { P = FVector2D(ParkMin.X + D, ParkMin.Y); }
		else if ((D -= WX) < WY) { P = FVector2D(ParkMax.X, ParkMin.Y + D); }
		else if ((D -= WY) < WX) { P = FVector2D(ParkMax.X - D, ParkMax.Y); }
		else { D -= WX; P = FVector2D(ParkMin.X, ParkMax.Y - D); }
		// Leave the side facing the cul-de-sac open.
		if (P.X < ParkMin.X + 10.0 && P.Y > -3200.0 && P.Y < -100.0)
		{
			continue;
		}
		P += FVector2D(Random.FRandRange(-300.0, 300.0), Random.FRandRange(-300.0, 300.0));
		BuildTree(FTransform(FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0), FVector(P.X, P.Y, NbGroundTop)), Random.FRand() < 0.4, Random.FRandRange(1.0, 1.4));
	}
}
