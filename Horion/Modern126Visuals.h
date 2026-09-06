#pragma once

#include "Modern126Gameplay.h"
#include <d2d1_1.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Read-only visual data bridge for the validated 1.26 runtime. This deliberately
// avoids the archived Horion entity/block SDK paths: current actors and blocks are
// sampled through the maintained 1.26 layouts, then drawn by the already-stable
// Direct2D Present renderer. No packet or anti-cheat behavior lives here.
namespace Modern126Visuals {
	struct Vec2Lite {
		float x;
		float y;
	};

	struct Vec3Lite {
		float x;
		float y;
		float z;
	};

	struct AabbLite {
		Vec3Lite lower;
		Vec3Lite higher;
	};

	struct AabbShapeLite {
		AabbLite boundingBox;
		Vec2Lite size;
	};

	struct StateVectorLite {
		Vec3Lite pos;
		Vec3Lite posOld;
		Vec3Lite velocity;
	};

	struct BlockPosLite {
		int x;
		int y;
		int z;
	};

	struct BlockHit {
		BlockPosLite pos;
		uint64_t hash;
	};

	struct Mat4Lite {
		float m[16];
	};

	struct ProjectionContext {
		Vec3Lite origin;
		Mat4Lite view;
		Mat4Lite projection;
		D2D1_SIZE_F screen;
	};

	inline std::vector<AabbLite> entityBoxes;
	inline std::vector<BlockHit> blockBoxes;
	inline std::vector<BlockHit> blockScanWorking;
	inline ULONGLONG lastEntityRefresh = 0;
	inline bool blockScanCenterValid = false;
	inline BlockPosLite blockScanCenter = {};
	inline size_t blockScanCursor = 0;
	inline bool loggedEntityReady = false;
	inline bool loggedEntityFailure = false;
	inline bool loggedProjectionReady = false;
	inline bool loggedProjectionFailure = false;
	inline bool loggedBlockReady = false;
	inline bool loggedBlockFailure = false;

	inline bool readableSpan(const void* address, size_t requiredBytes) {
		if (address == nullptr || requiredBytes == 0)
			return false;
		MEMORY_BASIC_INFORMATION info = {};
		if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT)
			return false;
		if ((info.Protect & PAGE_GUARD) != 0 || (info.Protect & PAGE_NOACCESS) != 0)
			return false;
		const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
		const uintptr_t end = begin + requiredBytes;
		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
		return end >= begin && end <= regionEnd;
	}

	template <typename T>
	inline bool safeRead(const void* address, T* out) {
		if (out == nullptr || !readableSpan(address, sizeof(T)))
			return false;
		__try {
			*out = *reinterpret_cast<const T*>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline bool addressInMinecraft(uintptr_t address) {
		if (address == 0)
			return false;
		MEMORY_BASIC_INFORMATION info = {};
		const SIZE_T queried = VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info));
		return queried == sizeof(info) && info.Type == MEM_IMAGE &&
			info.AllocationBase == GetModuleHandleA("Minecraft.Windows.exe");
	}

	inline bool finiteVec3(const Vec3Lite& value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	inline bool finiteMatrix(const Mat4Lite& value) {
		for (float cell : value.m) {
			if (!std::isfinite(cell))
				return false;
		}
		return true;
	}

	inline void* resolveMinecraftGame() {
		void* ci = Modern126Gameplay::clientInstance;
		if (ci == nullptr)
			return nullptr;
		void* game = nullptr;
		return safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(ci) + 0x1A0), &game) ? game : nullptr;
	}

	inline void* resolveLevel() {
		void* ci = Modern126Gameplay::clientInstance;
		if (ci == nullptr)
			return nullptr;

		void* minecraft = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(ci) + 0x1A8), &minecraft) || minecraft == nullptr)
			return nullptr;

		void* session = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(minecraft) + 0xC0), &session) || session == nullptr)
			return nullptr;

		uint8_t sessionReady = 0;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(session) + 0x28), &sessionReady) || sessionReady != 1)
			return nullptr;

		uint8_t* controlBlock = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(session) + 0x30), &controlBlock) || controlBlock == nullptr)
			return nullptr;
		uint8_t controlReady = 0;
		if (!safeRead(controlBlock, &controlReady) || controlReady != 1)
			return nullptr;

		void* level = nullptr;
		return safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(session) + 0x40), &level) ? level : nullptr;
	}

	inline void* callGetRegion(void* ci, uintptr_t target) {
		__try {
			using Fn = void*(__fastcall*)(void*);
			return reinterpret_cast<Fn>(target)(ci);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	inline void* resolveRegion() {
		void* ci = Modern126Gameplay::clientInstance;
		if (ci == nullptr)
			return nullptr;
		uintptr_t* vtable = nullptr;
		if (!safeRead(ci, &vtable) || vtable == nullptr)
			return nullptr;
		uintptr_t target = 0;
		if (!safeRead(vtable + 0x1E, &target) || !addressInMinecraft(target))
			return nullptr;
		return callGetRegion(ci, target);
	}

	inline bool callGetRuntimeActorList(void* level, uintptr_t target, std::vector<void*>& out) {
		__try {
			using Fn = void(__fastcall*)(void*, std::vector<void*>&);
			reinterpret_cast<Fn>(target)(level, out);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline bool resolveActorPosition(void* actor, Vec3Lite* out) {
		if (actor == nullptr || out == nullptr)
			return false;
		StateVectorLite* state = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(actor) + 0x218), &state) || state == nullptr)
			return false;
		StateVectorLite copy = {};
		if (!safeRead(state, &copy) || !finiteVec3(copy.pos))
			return false;
		*out = copy.pos;
		return true;
	}

	inline bool resolveActorBox(void* actor, AabbLite* out) {
		if (actor == nullptr || out == nullptr)
			return false;
		AabbShapeLite* shape = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(actor) + 0x220), &shape) || shape == nullptr)
			return false;
		AabbShapeLite copy = {};
		if (!safeRead(shape, &copy) || !finiteVec3(copy.boundingBox.lower) || !finiteVec3(copy.boundingBox.higher))
			return false;

		const float width = copy.boundingBox.higher.x - copy.boundingBox.lower.x;
		const float height = copy.boundingBox.higher.y - copy.boundingBox.lower.y;
		const float depth = copy.boundingBox.higher.z - copy.boundingBox.lower.z;
		if (width <= 0.01f || height <= 0.01f || depth <= 0.01f || width > 20.0f || height > 20.0f || depth > 20.0f)
			return false;

		*out = copy.boundingBox;
		return true;
	}

	inline bool refreshEntityBoxes() {
		const ULONGLONG now = GetTickCount64();
		if (now - lastEntityRefresh < 100)
			return true;
		lastEntityRefresh = now;

		void* level = resolveLevel();
		if (level == nullptr)
			return false;

		uintptr_t* vtable = nullptr;
		if (!safeRead(level, &vtable) || vtable == nullptr)
			return false;
		uintptr_t listTarget = 0;
		if (!safeRead(vtable + 0x145, &listTarget) || !addressInMinecraft(listTarget))
			return false;

		std::vector<void*> actors;
		if (!callGetRuntimeActorList(level, listTarget, actors)) {
			if (!loggedEntityFailure) {
				loggedEntityFailure = true;
				logF("[modern] ESP actor snapshot call failed; visual boxes skipped");
			}
			return false;
		}

		void* localPlayer = Modern126Gameplay::refreshLocalPlayer();
		Vec3Lite localPos = {};
		const bool haveLocalPos = resolveActorPosition(localPlayer, &localPos);

		std::vector<AabbLite> next;
		next.reserve(std::min<size_t>(actors.size(), 256));
		for (void* actor : actors) {
			if (actor == nullptr || actor == localPlayer)
				continue;
			AabbLite box = {};
			if (!resolveActorBox(actor, &box))
				continue;
			if (haveLocalPos) {
				const Vec3Lite center = {
					(box.lower.x + box.higher.x) * 0.5f,
					(box.lower.y + box.higher.y) * 0.5f,
					(box.lower.z + box.higher.z) * 0.5f
				};
				const float dx = center.x - localPos.x;
				const float dy = center.y - localPos.y;
				const float dz = center.z - localPos.z;
				if ((dx * dx + dy * dy + dz * dz) > (128.0f * 128.0f))
					continue;
			}
			next.push_back(box);
			if (next.size() >= 512)
				break;
		}
		entityBoxes.swap(next);
		loggedEntityFailure = false;
		if (!loggedEntityReady) {
			loggedEntityReady = true;
			logF("[modern] ESP actor snapshot bridge READY actors=%zu slot=0x145", entityBoxes.size());
		}
		return true;
	}

	constexpr uint64_t fnvBlockHash(const char* text, size_t length) {
		uint64_t hash = 0xcbf29ce484222325ull;
		for (size_t i = 0; i < length; ++i) {
			hash *= 0x100000001b3ull;
			hash ^= static_cast<uint8_t>(text[i]);
		}
		return hash;
	}

	template <size_t N>
	constexpr uint64_t fnvBlockHash(const char (&text)[N]) {
		return fnvBlockHash(text, N - 1);
	}

	inline constexpr uint64_t diamondOreHash = fnvBlockHash("minecraft:diamond_ore");
	inline constexpr uint64_t deepslateDiamondOreHash = fnvBlockHash("minecraft:deepslate_diamond_ore");
	inline constexpr uint64_t emeraldOreHash = fnvBlockHash("minecraft:emerald_ore");
	inline constexpr uint64_t deepslateEmeraldOreHash = fnvBlockHash("minecraft:deepslate_emerald_ore");
	inline constexpr uint64_t goldOreHash = fnvBlockHash("minecraft:gold_ore");
	inline constexpr uint64_t deepslateGoldOreHash = fnvBlockHash("minecraft:deepslate_gold_ore");
	inline constexpr uint64_t netherGoldOreHash = fnvBlockHash("minecraft:nether_gold_ore");
	inline constexpr uint64_t ancientDebrisHash = fnvBlockHash("minecraft:ancient_debris");

	inline bool valuableBlock(uint64_t hash) {
		return hash == diamondOreHash || hash == deepslateDiamondOreHash ||
			hash == emeraldOreHash || hash == deepslateEmeraldOreHash ||
			hash == goldOreHash || hash == deepslateGoldOreHash ||
			hash == netherGoldOreHash || hash == ancientDebrisHash;
	}

	inline void* callGetBlock(void* region, uintptr_t target, const BlockPosLite& pos) {
		__try {
			using Fn = void*(__fastcall*)(void*, const BlockPosLite&);
			return reinterpret_cast<Fn>(target)(region, pos);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	inline bool resolveBlockHash(void* region, uintptr_t getBlockTarget, const BlockPosLite& pos, uint64_t* outHash) {
		if (outHash == nullptr)
			return false;
		void* block = callGetBlock(region, getBlockTarget, pos);
		if (block == nullptr)
			return false;
		void* legacy = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(block) + 0x68), &legacy) || legacy == nullptr)
			return false;
		int64_t signedHash = 0;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(legacy) + 0xE0), &signedHash))
			return false;
		*outHash = static_cast<uint64_t>(signedHash);
		return true;
	}

	inline void beginBlockScan(const BlockPosLite& center) {
		blockScanCenter = center;
		blockScanCenterValid = true;
		blockScanCursor = 0;
		blockScanWorking.clear();
	}

	inline bool refreshBlockBoxes() {
		void* localPlayer = Modern126Gameplay::refreshLocalPlayer();
		Vec3Lite localPos = {};
		if (!resolveActorPosition(localPlayer, &localPos))
			return false;

		const BlockPosLite currentCenter = {
			static_cast<int>(std::floor(localPos.x)),
			static_cast<int>(std::floor(localPos.y)),
			static_cast<int>(std::floor(localPos.z))
		};
		if (!blockScanCenterValid ||
			std::abs(currentCenter.x - blockScanCenter.x) > 4 ||
			std::abs(currentCenter.y - blockScanCenter.y) > 4 ||
			std::abs(currentCenter.z - blockScanCenter.z) > 4) {
			beginBlockScan(currentCenter);
		}

		void* region = resolveRegion();
		if (region == nullptr)
			return false;
		uintptr_t* vtable = nullptr;
		if (!safeRead(region, &vtable) || vtable == nullptr)
			return false;
		uintptr_t getBlockTarget = 0;
		if (!safeRead(vtable + 0x2, &getBlockTarget) || !addressInMinecraft(getBlockTarget)) {
			if (!loggedBlockFailure) {
				loggedBlockFailure = true;
				logF("[modern] BlockESP disabled for frame: BlockSource::getBlock slot 0x2 failed validation");
			}
			return false;
		}

		constexpr int radius = 12;
		constexpr int side = radius * 2 + 1;
		constexpr size_t total = static_cast<size_t>(side) * side * side;
		constexpr size_t blocksPerFrame = 128;

		for (size_t work = 0; work < blocksPerFrame && blockScanCursor < total; ++work, ++blockScanCursor) {
			const size_t index = blockScanCursor;
			const int dx = static_cast<int>(index % side) - radius;
			const int dz = static_cast<int>((index / side) % side) - radius;
			const int dy = static_cast<int>((index / (side * side)) % side) - radius;
			const BlockPosLite pos = {
				blockScanCenter.x + dx,
				blockScanCenter.y + dy,
				blockScanCenter.z + dz
			};
			uint64_t hash = 0;
			if (resolveBlockHash(region, getBlockTarget, pos, &hash) && valuableBlock(hash))
				blockScanWorking.push_back({ pos, hash });
		}

		if (blockScanCursor >= total) {
			blockBoxes = blockScanWorking;
			if (!loggedBlockReady) {
				loggedBlockReady = true;
				logF("[modern] BlockESP scanner READY radius=%d valuableBlocks=%zu preset=diamond/emerald/gold/ancient_debris",
					radius, blockBoxes.size());
			}
			loggedBlockFailure = false;
			beginBlockScan(currentCenter);
		}
		return true;
	}

	inline bool createProjectionContext(ID2D1Bitmap1* target, ProjectionContext* out) {
		if (target == nullptr || out == nullptr)
			return false;
		void* ci = Modern126Gameplay::clientInstance;
		void* minecraftGame = resolveMinecraftGame();
		if (ci == nullptr || minecraftGame == nullptr)
			return false;

		void* levelRenderer = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(ci) + 0x1B8), &levelRenderer) || levelRenderer == nullptr)
			return false;
		void* levelRendererPlayer = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(levelRenderer) + 0x468), &levelRendererPlayer) || levelRendererPlayer == nullptr)
			return false;

		ProjectionContext next = {};
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(levelRendererPlayer) + 0x660), &next.origin) || !finiteVec3(next.origin))
			return false;

		void* gameRenderer = nullptr;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(minecraftGame) + 0x1318), &gameRenderer) || gameRenderer == nullptr)
			return false;
		if (!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(gameRenderer) + 0x380), &next.view) ||
			!safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(gameRenderer) + 0x400), &next.projection) ||
			!finiteMatrix(next.view) || !finiteMatrix(next.projection))
			return false;

		next.screen = target->GetSize();
		if (next.screen.width < 32.0f || next.screen.height < 32.0f)
			return false;
		*out = next;
		if (!loggedProjectionReady) {
			loggedProjectionReady = true;
			logF("[modern] ESP projection bridge READY renderer=%llX origin=(%.1f, %.1f, %.1f)",
				reinterpret_cast<uintptr_t>(gameRenderer), next.origin.x, next.origin.y, next.origin.z);
		}
		loggedProjectionFailure = false;
		return true;
	}

	inline std::array<float, 4> transform(const Mat4Lite& matrix, const std::array<float, 4>& value) {
		return {
			matrix.m[0] * value[0] + matrix.m[4] * value[1] + matrix.m[8] * value[2] + matrix.m[12] * value[3],
			matrix.m[1] * value[0] + matrix.m[5] * value[1] + matrix.m[9] * value[2] + matrix.m[13] * value[3],
			matrix.m[2] * value[0] + matrix.m[6] * value[1] + matrix.m[10] * value[2] + matrix.m[14] * value[3],
			matrix.m[3] * value[0] + matrix.m[7] * value[1] + matrix.m[11] * value[2] + matrix.m[15] * value[3]
		};
	}

	inline bool projectPoint(const Vec3Lite& world, const ProjectionContext& context, D2D1_POINT_2F* out) {
		if (out == nullptr)
			return false;
		const std::array<float, 4> relative = {
			world.x - context.origin.x,
			world.y - context.origin.y,
			world.z - context.origin.z,
			1.0f
		};
		const auto view = transform(context.view, relative);
		const auto clip = transform(context.projection, view);
		if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[3]) || clip[3] < 0.05f)
			return false;
		const float invW = 1.0f / clip[3];
		const float ndcX = clip[0] * invW;
		const float ndcY = clip[1] * invW;
		out->x = (ndcX + 1.0f) * 0.5f * context.screen.width;
		out->y = (1.0f - ndcY) * 0.5f * context.screen.height;
		return std::isfinite(out->x) && std::isfinite(out->y);
	}

	inline std::array<Vec3Lite, 8> boxCorners(const AabbLite& box) {
		return {
			Vec3Lite { box.lower.x, box.lower.y, box.lower.z },
			Vec3Lite { box.higher.x, box.lower.y, box.lower.z },
			Vec3Lite { box.higher.x, box.higher.y, box.lower.z },
			Vec3Lite { box.lower.x, box.higher.y, box.lower.z },
			Vec3Lite { box.lower.x, box.lower.y, box.higher.z },
			Vec3Lite { box.higher.x, box.lower.y, box.higher.z },
			Vec3Lite { box.higher.x, box.higher.y, box.higher.z },
			Vec3Lite { box.lower.x, box.higher.y, box.higher.z }
		};
	}

	inline bool projectBox(const AabbLite& box, const ProjectionContext& context, std::array<D2D1_POINT_2F, 8>* out) {
		if (out == nullptr)
			return false;
		const auto corners = boxCorners(box);
		for (size_t i = 0; i < corners.size(); ++i) {
			if (!projectPoint(corners[i], context, &(*out)[i]))
				return false;
		}
		return true;
	}

	inline void drawEntityEsp(ID2D1DeviceContext* context, ID2D1SolidColorBrush* brush, const ProjectionContext& projection) {
		if (context == nullptr || brush == nullptr)
			return;
		refreshEntityBoxes();
		for (const auto& box : entityBoxes) {
			std::array<D2D1_POINT_2F, 8> points = {};
			if (!projectBox(box, projection, &points))
				continue;
			float minX = points[0].x;
			float minY = points[0].y;
			float maxX = points[0].x;
			float maxY = points[0].y;
			for (size_t i = 1; i < points.size(); ++i) {
				minX = std::min(minX, points[i].x);
				minY = std::min(minY, points[i].y);
				maxX = std::max(maxX, points[i].x);
				maxY = std::max(maxY, points[i].y);
			}
			if (maxX - minX < 2.0f || maxY - minY < 2.0f || maxX - minX > projection.screen.width * 2.0f ||
				maxY - minY > projection.screen.height * 2.0f)
				continue;
			context->DrawRectangle(D2D1::RectF(minX, minY, maxX, maxY), brush, 2.0f);
		}
	}

	inline void drawWireBox(ID2D1DeviceContext* context, ID2D1SolidColorBrush* brush,
		const std::array<D2D1_POINT_2F, 8>& points, float thickness) {
		static constexpr int edges[12][2] = {
			{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
			{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
		};
		for (const auto& edge : edges)
			context->DrawLine(points[edge[0]], points[edge[1]], brush, thickness);
	}

	inline void drawBlockEsp(ID2D1DeviceContext* context, ID2D1SolidColorBrush* brush, const ProjectionContext& projection) {
		if (context == nullptr || brush == nullptr)
			return;
		refreshBlockBoxes();
		for (const auto& hit : blockBoxes) {
			const AabbLite box = {
				{ static_cast<float>(hit.pos.x), static_cast<float>(hit.pos.y), static_cast<float>(hit.pos.z) },
				{ static_cast<float>(hit.pos.x + 1), static_cast<float>(hit.pos.y + 1), static_cast<float>(hit.pos.z + 1) }
			};
			std::array<D2D1_POINT_2F, 8> points = {};
			if (!projectBox(box, projection, &points))
				continue;
			drawWireBox(context, brush, points, 1.6f);
		}
	}

	inline void render(ID2D1DeviceContext* context, ID2D1Bitmap1* target,
		ID2D1SolidColorBrush* espBrush, ID2D1SolidColorBrush* blockBrush,
		bool espEnabled, bool blockEspEnabled) {
		if ((!espEnabled && !blockEspEnabled) || context == nullptr || target == nullptr)
			return;

		ProjectionContext projection = {};
		if (!createProjectionContext(target, &projection)) {
			if (!loggedProjectionFailure) {
				loggedProjectionFailure = true;
				logF("[modern] ESP projection bridge unavailable for this frame; visuals skipped");
			}
			return;
		}

		if (espEnabled)
			drawEntityEsp(context, espBrush, projection);
		if (blockEspEnabled)
			drawBlockEsp(context, blockBrush, projection);
	}

	inline void shutdown() {
		entityBoxes.clear();
		blockBoxes.clear();
		blockScanWorking.clear();
		blockScanCenterValid = false;
		blockScanCursor = 0;
		lastEntityRefresh = 0;
	}
}
