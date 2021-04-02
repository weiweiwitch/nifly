/*
nifly
C++ NIF library for the Gamebryo/NetImmerse File Format
See the included GPLv3 LICENSE file
*/

#pragma once

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "Nodes.hpp"

namespace nifly {
class NiParticles : public NiObjectCRTP<NiParticles, NiGeometry> {
public:
	static constexpr const char* BlockName = "NiParticles";
	const char* GetBlockName() override { return BlockName; }
};

class NiAutoNormalParticles : public NiObjectCRTP<NiAutoNormalParticles, NiParticles> {
public:
	static constexpr const char* BlockName = "NiAutoNormalParticles";
	const char* GetBlockName() override { return BlockName; }
};

class NiParticleMeshes : public NiObjectCRTP<NiParticleMeshes, NiParticles> {
public:
	static constexpr const char* BlockName = "NiParticleMeshes";
	const char* GetBlockName() override { return BlockName; }
};

class NiRotatingParticles : public NiObjectCRTP<NiRotatingParticles, NiParticles> {
public:
	static constexpr const char* BlockName = "NiRotatingParticles";
	const char* GetBlockName() override { return BlockName; }
};

class NiParticlesData : public NiObjectCRTP<NiParticlesData, NiGeometryData, true> {
protected:
	uint32_t numSubtexOffsets = 0;
	std::vector<Vector4> subtexOffsets;

public:
	bool hasRadii = false;
	uint16_t numActive = 0;
	bool hasSizes = false;
	bool hasRotations = false;
	bool hasRotationAngles = false;
	bool hasRotationAxes = false;
	bool hasTextureIndices = false;

	float aspectRatio = 0.0f;
	uint16_t aspectFlags = 0;
	float speedToAspectAspect2 = 0.0f;
	float speedToAspectSpeed1 = 0.0f;
	float speedToAspectSpeed2 = 0.0f;

	NiParticlesData();

	static constexpr const char* BlockName = "NiParticlesData";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);

	std::vector<Vector4> GetSubtexOffsets() const;
	void SetSubtexOffsets(const std::vector<Vector4>& sto);
};

class NiAutoNormalParticlesData : public NiObjectCRTP<NiAutoNormalParticlesData, NiParticlesData> {
public:
	static constexpr const char* BlockName = "NiAutoNormalParticlesData";
	const char* GetBlockName() override { return BlockName; }
};

class NiRotatingParticlesData : public NiObjectCRTP<NiRotatingParticlesData, NiParticlesData> {
public:
	static constexpr const char* BlockName = "NiRotatingParticlesData";
	const char* GetBlockName() override { return BlockName; }
};

class NiParticleMeshesData : public NiObjectCRTP<NiParticleMeshesData, NiRotatingParticlesData, true> {
public:
	NiBlockRef<NiAVObject> dataRef;

	static constexpr const char* BlockName = "NiParticleMeshesData";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class NiPSysData : public NiObjectCRTP<NiPSysData, NiRotatingParticlesData, true> {
public:
	Vector3 unknownVector;
	bool hasRotationSpeeds = false;

	static constexpr const char* BlockName = "NiPSysData";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiMeshPSysData : public NiObjectCRTP<NiMeshPSysData, NiPSysData, true> {
public:
	uint32_t defaultPoolSize = 0;
	bool fillPoolsOnLoad = false;

	NiVector<uint32_t> generationPoolSize;
	NiBlockRef<NiNode> nodeRef;

	static constexpr const char* BlockName = "NiMeshPSysData";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class BSStripPSysData : public NiObjectCRTP<BSStripPSysData, NiPSysData, true> {
public:
	uint16_t maxPointCount = 0;
	uint32_t startCapSize = 0;
	uint32_t endCapSize = 0;
	bool doZPrepass = false;

	static constexpr const char* BlockName = "BSStripPSysData";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysEmitterCtlrData : public NiObjectCRTP<NiPSysEmitterCtlrData, NiObject, true> {
protected:
	uint32_t numVisibilityKeys = 0;
	std::vector<Key<uint8_t>> visibilityKeys;

public:
	KeyGroup<float> floatKeys;

	static constexpr const char* BlockName = "NiPSysEmitterCtlrData";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);

	std::vector<Key<uint8_t>> GetVisibilityKeys() const;
	void SetVisibilityKeys(const std::vector<Key<uint8_t>>& vk);
};

class NiParticleSystem;

class NiPSysModifier : public NiObjectCRTP<NiPSysModifier, NiObject, true> {
public:
	NiStringRef name;
	uint32_t order = 0;
	NiBlockPtr<NiParticleSystem> targetRef;
	bool isActive = false;

	void Sync(NiStreamReversible& stream);
	void GetStringRefs(std::vector<NiStringRef*>& refs) override;
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};

class BSPSysStripUpdateModifier : public NiObjectCRTP<BSPSysStripUpdateModifier, NiPSysModifier, true> {
public:
	float updateDeltaTime = 0.0f;

	static constexpr const char* BlockName = "BSPSysStripUpdateModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysSpawnModifier : public NiObjectCRTP<NiPSysSpawnModifier, NiPSysModifier, true> {
public:
	uint16_t numSpawnGenerations = 0;
	float percentSpawned = 0.0f;
	uint16_t minSpawned = 0;
	uint16_t maxSpawned = 0;
	float spawnSpeedVariation = 0.0f;
	float spawnDirVariation = 0.0f;
	float lifeSpan = 0.0f;
	float lifeSpanVariation = 0.0f;

	static constexpr const char* BlockName = "NiPSysSpawnModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysAgeDeathModifier : public NiObjectCRTP<NiPSysAgeDeathModifier, NiPSysModifier, true> {
public:
	bool spawnOnDeath = false;
	NiBlockRef<NiPSysSpawnModifier> spawnModifierRef;

	static constexpr const char* BlockName = "NiPSysAgeDeathModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class BSPSysLODModifier : public NiObjectCRTP<BSPSysLODModifier, NiPSysModifier, true> {
public:
	float lodBeginDistance = 0.1f;
	float lodEndDistance = 0.7f;
	float endEmitScale = 0.2f;
	float endSize = 1.0f;

	static constexpr const char* BlockName = "BSPSysLODModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class BSPSysSimpleColorModifier : public NiObjectCRTP<BSPSysSimpleColorModifier, NiPSysModifier, true> {
public:
	float fadeInPercent = 0.0f;
	float fadeOutPercent = 0.0f;
	float color1EndPercent = 0.0f;
	float color2StartPercent = 0.0f;
	float color2EndPercent = 0.0f;
	float color3StartPercent = 0.0f;
	Color4 color1;
	Color4 color2;
	Color4 color3;
	uint16_t unknownShorts[26]{};

	static constexpr const char* BlockName = "BSPSysSimpleColorModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysRotationModifier : public NiObjectCRTP<NiPSysRotationModifier, NiPSysModifier, true> {
public:
	float initialSpeed = 0.0f;
	float initialSpeedVariation = 0.0f;
	Vector4 unknownVector;
	uint8_t unknownByte = 0;
	float initialAngle = 0.0f;
	float initialAngleVariation = 0.0f;
	bool randomSpeedSign = false;
	bool randomInitialAxis = false;
	Vector3 initialAxis;

	static constexpr const char* BlockName = "NiPSysRotationModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class BSPSysScaleModifier : public NiObjectCRTP<BSPSysScaleModifier, NiPSysModifier, true> {
public:
	NiVector<float> floats;

	static constexpr const char* BlockName = "BSPSysScaleModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

enum ForceType : uint32_t { FORCE_PLANAR, FORCE_SPHERICAL, FORCE_UNKNOWN };

class NiPSysGravityModifier : public NiObjectCRTP<NiPSysGravityModifier, NiPSysModifier, true> {
public:
	NiBlockPtr<NiNode> gravityObjRef;
	Vector3 gravityAxis;
	float decay = 0.0f;
	float strength = 0.0f;
	ForceType forceType = FORCE_UNKNOWN;
	float turbulence = 0.0f;
	float turbulenceScale = 1.0f;
	bool worldAligned = false;

	static constexpr const char* BlockName = "NiPSysGravityModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};

class NiPSysPositionModifier : public NiObjectCRTP<NiPSysPositionModifier, NiPSysModifier> {
public:
	static constexpr const char* BlockName = "NiPSysPositionModifier";
	const char* GetBlockName() override { return BlockName; }
};

class NiPSysBoundUpdateModifier : public NiObjectCRTP<NiPSysBoundUpdateModifier, NiPSysModifier, true> {
public:
	uint16_t updateSkip = 0;

	static constexpr const char* BlockName = "NiPSysBoundUpdateModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysDragModifier : public NiObjectCRTP<NiPSysDragModifier, NiPSysModifier, true> {
public:
	NiBlockPtr<NiObject> parentRef;
	Vector3 dragAxis;
	float percentage = 0.0f;
	float range = 0.0f;
	float rangeFalloff = 0.0f;

	static constexpr const char* BlockName = "NiPSysDragModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};

class BSPSysInheritVelocityModifier
	: public NiObjectCRTP<BSPSysInheritVelocityModifier, NiPSysModifier, true> {
public:
	NiBlockPtr<NiNode> targetNodeRef;
	float changeToInherit = 0.0f;
	float velocityMult = 0.0f;
	float velocityVar = 0.0f;

	static constexpr const char* BlockName = "BSPSysInheritVelocityModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};

class BSPSysSubTexModifier : public NiObjectCRTP<BSPSysSubTexModifier, NiPSysModifier, true> {
public:
	float startFrame = 0.0f;
	float startFrameVariation = 0.0f;
	float endFrame = 0.0f;
	float loopStartFrame = 0.0f;
	float loopStartFrameVariation = 0.0f;
	float frameCount = 0.0f;
	float frameCountVariation = 0.0f;

	static constexpr const char* BlockName = "BSPSysSubTexModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

enum DecayType : uint32_t { DECAY_NONE, DECAY_LINEAR, DECAY_EXPONENTIAL };

enum SymmetryType : uint32_t { SYMMETRY_SPHERICAL, SYMMETRY_CYLINDRICAL, SYMMETRY_PLANAR };

class NiPSysBombModifier : public NiObjectCRTP<NiPSysBombModifier, NiPSysModifier, true> {
public:
	NiBlockPtr<NiNode> bombNodeRef;
	Vector3 bombAxis;
	float decay = 0.0f;
	float deltaV = 0.0f;
	DecayType decayType = DECAY_NONE;
	SymmetryType symmetryType = SYMMETRY_SPHERICAL;

	static constexpr const char* BlockName = "NiPSysBombModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};

class NiColorData : public NiObjectCRTP<NiColorData, NiObject, true> {
public:
	KeyGroup<Color4> data;

	static constexpr const char* BlockName = "NiColorData";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysColorModifier : public NiObjectCRTP<NiPSysColorModifier, NiPSysModifier, true> {
public:
	NiBlockRef<NiColorData> dataRef;

	static constexpr const char* BlockName = "NiPSysColorModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class NiPSysGrowFadeModifier : public NiObjectCRTP<NiPSysGrowFadeModifier, NiPSysModifier, true> {
public:
	float growTime = 0.0f;
	uint16_t growGeneration = 0;
	float fadeTime = 0.0f;
	uint16_t fadeGeneration = 0;
	float baseScale = 0.0f;

	static constexpr const char* BlockName = "NiPSysGrowFadeModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysMeshUpdateModifier : public NiObjectCRTP<NiPSysMeshUpdateModifier, NiPSysModifier, true> {
public:
	NiBlockRefArray<NiAVObject> meshRefs;

	static constexpr const char* BlockName = "NiPSysMeshUpdateModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class NiPSysFieldModifier : public NiObjectCRTP<NiPSysFieldModifier, NiPSysModifier, true> {
public:
	NiBlockRef<NiAVObject> fieldObjectRef;
	float magnitude = 0.0f;
	float attenuation = 0.0f;
	bool useMaxDistance = false;
	float maxDistance = 0.0f;

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class NiPSysVortexFieldModifier : public NiObjectCRTP<NiPSysVortexFieldModifier, NiPSysFieldModifier, true> {
public:
	Vector3 direction;

	static constexpr const char* BlockName = "NiPSysVortexFieldModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysGravityFieldModifier
	: public NiObjectCRTP<NiPSysGravityFieldModifier, NiPSysFieldModifier, true> {
public:
	Vector3 direction;

	static constexpr const char* BlockName = "NiPSysGravityFieldModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysDragFieldModifier : public NiObjectCRTP<NiPSysDragFieldModifier, NiPSysFieldModifier, true> {
public:
	bool useDirection = false;
	Vector3 direction;

	static constexpr const char* BlockName = "NiPSysDragFieldModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysTurbulenceFieldModifier
	: public NiObjectCRTP<NiPSysTurbulenceFieldModifier, NiPSysFieldModifier, true> {
public:
	float frequency = 0.0f;

	static constexpr const char* BlockName = "NiPSysTurbulenceFieldModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysAirFieldModifier : public NiObjectCRTP<NiPSysAirFieldModifier, NiPSysFieldModifier, true> {
public:
	Vector3 direction;
	float airFriction = 0.0f;
	float inheritVelocity = 0.0f;
	bool inheritRotation = false;
	bool componentOnly = false;
	bool enableSpread = false;
	float spread = 0.0f;

	static constexpr const char* BlockName = "NiPSysAirFieldModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysRadialFieldModifier : public NiObjectCRTP<NiPSysRadialFieldModifier, NiPSysFieldModifier, true> {
public:
	uint32_t radialType = 0;

	static constexpr const char* BlockName = "NiPSysRadialFieldModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class BSWindModifier : public NiObjectCRTP<BSWindModifier, NiPSysModifier, true> {
public:
	float strength = 0.0f;

	static constexpr const char* BlockName = "BSWindModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class BSPSysRecycleBoundModifier : public NiObjectCRTP<BSPSysRecycleBoundModifier, NiPSysModifier, true> {
public:
	Vector3 boundOffset;
	Vector3 boundExtent;
	NiBlockPtr<NiNode> targetNodeRef;

	static constexpr const char* BlockName = "BSPSysRecycleBoundModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};

class BSPSysHavokUpdateModifier : public NiObjectCRTP<BSPSysHavokUpdateModifier, NiPSysModifier, true> {
public:
	NiBlockRefArray<NiNode> nodeRefs;
	NiBlockRef<NiPSysModifier> modifierRef;

	static constexpr const char* BlockName = "BSPSysHavokUpdateModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class BSParentVelocityModifier : public NiObjectCRTP<BSParentVelocityModifier, NiPSysModifier, true> {
public:
	float damping = 0.0f;

	static constexpr const char* BlockName = "BSParentVelocityModifier";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class BSMasterParticleSystem : public NiObjectCRTP<BSMasterParticleSystem, NiNode, true> {
public:
	uint16_t maxEmitterObjs = 0;
	NiBlockRefArray<NiAVObject> particleSysRefs;

	static constexpr const char* BlockName = "BSMasterParticleSystem";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);

	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class NiParticleSystem : public NiObjectCRTP<NiParticleSystem, NiAVObject, true> {
protected:
	uint32_t numMaterials = 0;
	std::vector<MaterialInfo> materials;

public:
	NiBlockRef<NiGeometryData> dataRef;
	NiBlockRef<NiObject> skinInstanceRef;
	NiBlockRef<NiProperty> shaderPropertyRef;
	NiBlockRef<NiProperty> alphaPropertyRef;

	uint32_t activeMaterial = 0;
	uint8_t defaultMatNeedsUpdate = 0;

	uint8_t vertFlags1 = 81;
	uint8_t vertFlags2 = 0;
	uint8_t vertFlags3 = 0;
	uint8_t vertFlags4 = 4;
	uint8_t vertFlags5 = 0;
	uint8_t vertFlags6 = 32;
	uint8_t vertFlags7 = 64;
	uint8_t vertFlags8 = 8;

	BoundingSphere bounds;
	float boundMinMax[6]{};

	uint16_t farBegin = 0;
	uint16_t farEnd = 0;
	uint16_t nearBegin = 0;
	uint16_t nearEnd = 0;

	NiBlockRef<NiPSysData> psysDataRef;

	bool isWorldSpace = false;
	NiBlockRefArray<NiPSysModifier> modifierRefs;

	static constexpr const char* BlockName = "NiParticleSystem";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetStringRefs(std::vector<NiStringRef*>& refs) override;
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;

	std::vector<MaterialInfo> GetMaterials() const;
	void SetMaterials(const std::vector<MaterialInfo>& mi);
};

class NiMeshParticleSystem : public NiObjectCRTP<NiMeshParticleSystem, NiParticleSystem> {
public:
	static constexpr const char* BlockName = "NiMeshParticleSystem";
	const char* GetBlockName() override { return BlockName; }
};

class BSStripParticleSystem : public NiObjectCRTP<BSStripParticleSystem, NiParticleSystem> {
public:
	static constexpr const char* BlockName = "BSStripParticleSystem";
	const char* GetBlockName() override { return BlockName; }
};

class NiPSysColliderManager;

class NiPSysCollider : public NiObjectCRTP<NiPSysCollider, NiObject, true> {
public:
	float bounce = 0.0f;
	bool spawnOnCollide = false;
	bool dieOnCollide = false;
	NiBlockRef<NiPSysSpawnModifier> spawnModifierRef;
	NiBlockPtr<NiPSysColliderManager> managerRef;
	NiBlockRef<NiPSysCollider> nextColliderRef;
	NiBlockPtr<NiNode> colliderNodeRef;

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};

class NiPSysSphericalCollider : public NiObjectCRTP<NiPSysSphericalCollider, NiPSysCollider, true> {
public:
	float radius = 0.0f;

	static constexpr const char* BlockName = "NiPSysSphericalCollider";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysPlanarCollider : public NiObjectCRTP<NiPSysPlanarCollider, NiPSysCollider, true> {
public:
	float width = 0.0f;
	float height = 0.0f;
	Vector3 xAxis;
	Vector3 yAxis;

	static constexpr const char* BlockName = "NiPSysPlanarCollider";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysColliderManager : public NiObjectCRTP<NiPSysColliderManager, NiPSysModifier, true> {
public:
	NiBlockRef<NiPSysCollider> colliderRef;

	static constexpr const char* BlockName = "NiPSysColliderManager";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetChildRefs(std::set<NiRef*>& refs) override;
	void GetChildIndices(std::vector<int>& indices) override;
};

class NiPSysEmitter : public NiObjectCRTP<NiPSysEmitter, NiPSysModifier, true> {
public:
	float speed = 0.0f;
	float speedVariation = 0.0f;
	float declination = 0.0f;
	float declinationVariation = 0.0f;
	float planarAngle = 0.0f;
	float planarAngleVariation = 0.0f;
	Color4 color;
	float radius = 0.0f;
	float radiusVariation = 0.0f;
	float lifeSpan = 0.0f;
	float lifeSpanVariation = 0.0f;

	void Sync(NiStreamReversible& stream);
};

class NiPSysVolumeEmitter : public NiObjectCRTP<NiPSysVolumeEmitter, NiPSysEmitter, true> {
public:
	NiBlockPtr<NiNode> emitterNodeRef;

	void Sync(NiStreamReversible& stream);
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};

class NiPSysSphereEmitter : public NiObjectCRTP<NiPSysSphereEmitter, NiPSysVolumeEmitter, true> {
public:
	float radius = 0.0f;

	static constexpr const char* BlockName = "NiPSysSphereEmitter";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysCylinderEmitter : public NiObjectCRTP<NiPSysCylinderEmitter, NiPSysVolumeEmitter, true> {
public:
	float radius = 0.0f;
	float height = 0.0f;

	static constexpr const char* BlockName = "NiPSysCylinderEmitter";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class NiPSysBoxEmitter : public NiObjectCRTP<NiPSysBoxEmitter, NiPSysVolumeEmitter, true> {
public:
	float width = 0.0f;
	float height = 0.0f;
	float depth = 0.0f;

	static constexpr const char* BlockName = "NiPSysBoxEmitter";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
};

class BSPSysArrayEmitter : public NiObjectCRTP<BSPSysArrayEmitter, NiPSysVolumeEmitter> {
public:
	static constexpr const char* BlockName = "BSPSysArrayEmitter";
	const char* GetBlockName() override { return BlockName; }
};

enum VelocityType : uint32_t { VELOCITY_USE_NORMALS, VELOCITY_USE_RANDOM, VELOCITY_USE_DIRECTION };

enum EmitFrom : uint32_t {
	EMIT_FROM_VERTICES,
	EMIT_FROM_FACE_CENTER,
	EMIT_FROM_EDGE_CENTER,
	EMIT_FROM_FACE_SURFACE,
	EMIT_FROM_EDGE_SURFACE
};

class NiPSysMeshEmitter : public NiObjectCRTP<NiPSysMeshEmitter, NiPSysEmitter, true> {
public:
	NiBlockPtrArray<NiAVObject> meshRefs;
	VelocityType velocityType = VELOCITY_USE_NORMALS;
	EmitFrom emissionType = EMIT_FROM_VERTICES;
	Vector3 emissionAxis;

	static constexpr const char* BlockName = "NiPSysMeshEmitter";
	const char* GetBlockName() override { return BlockName; }

	void Sync(NiStreamReversible& stream);
	void GetPtrs(std::set<NiPtr*>& ptrs) override;
};
} // namespace nifly
