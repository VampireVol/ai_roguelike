#include "raylib.h"
#include <flecs.h>
#include <algorithm>

#include "ecsTypes.h"
#include "shootEmUp.h"
#include "dungeonGen.h"
#include "math.h"
#include "pathfinder.h"

static void update_camera(flecs::world &ecs)
{
  static auto cameraQuery = ecs.query<Camera2D>();
  static auto playerQuery = ecs.query<const Position, const IsPlayer>();

  cameraQuery.each([&](Camera2D &cam)
  {
    playerQuery.each([&](const Position &pos, const IsPlayer &)
    {
      cam.target.x += (pos.x - cam.target.x) * 0.1f;
      cam.target.y += (pos.y - cam.target.y) * 0.1f;
    });
  });
}

constexpr float tile_size = 64.f;

static IVec2 from_world_to_map(IVec2 p)
{
  return { p.x / (int)tile_size, p.y / (int)tile_size };
}

int main(int /*argc*/, const char ** /*argv*/)
{
  int width = 1920;
  int height = 1080 - 40;
  InitWindow(width, height, "w6 AI MIPT");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  flecs::world ecs;
  {
    constexpr size_t dungWidth = 50;
    constexpr size_t dungHeight = 50;
    char *tiles = new char[dungWidth * dungHeight];
    gen_drunk_dungeon(tiles, dungWidth, dungHeight);
    init_dungeon(ecs, tiles, dungWidth, dungHeight);
  }
  init_shoot_em_up(ecs);

  Camera2D camera = { {0, 0}, {0, 0}, 0.f, 1.f };
  camera.target = Vector2{ 0.f, 0.f };
  camera.offset = Vector2{ width * 0.5f, height * 0.5f };
  camera.rotation = 0.f;
  camera.zoom = 1.f;
  ecs.entity("camera")
    .set(Camera2D{camera});

  IVec2 from { -1, -1 };
  IVec2 to { -1, -1 };
  std::vector<PortalConnection> path;

  SetTargetFPS(60);               // Set our game to run at 60 frames-per-second
  while (!WindowShouldClose())
  {
    static auto cameraQuery = ecs.query<Camera2D>();
    static auto pathfinderQuery = ecs.query<const DungeonData, const DungeonPortals>();
    auto pathfindingProcess = [&]()
    {
      pathfinderQuery.each([&](const DungeonData &dd, const DungeonPortals &dp)
      {
        if (to != IVec2{ -1, -1 } && from != IVec2{ -1, -1 })
        {
          IVec2 mapFrom = from_world_to_map(from);
          IVec2 mapTo = from_world_to_map(to);
          size_t fromIdx = coord_to_tile_idx(mapFrom.x, mapFrom.y, dd.width);
          size_t toIdx = coord_to_tile_idx(mapTo.x, mapTo.y, dd.width);
          if (fromIdx == toIdx)
          {
            path = find_path_a_star_tiled(dd, dp, mapFrom, mapTo);
            size_t splitTiles = pathfinder::splitTiles;
            size_t tilePerRow = dd.width / splitTiles;
            size_t row = fromIdx / tilePerRow;
            size_t col = fromIdx % tilePerRow;
            IVec2 limMin{ int((col + 0) * splitTiles), int((row + 0) * splitTiles) };
            IVec2 limMax{ int((col + 1) * splitTiles), int((row + 1) * splitTiles) };
            auto pathSimple = find_path_a_star(dd, mapFrom, mapTo, limMin, limMax);
            if (pathSimple.empty())
            {
              path = find_path_a_star_tiled(dd, dp, mapFrom, mapTo);
            }
            else
            {
              path.clear();
              path.push_back({ size_t(-1), (float)pathSimple.size() });
            }
          }
          else
          {
            path = find_path_a_star_tiled(dd, dp, mapFrom, mapTo);
          }
        }
      });
    };
    process_game(ecs);
    update_camera(ecs);
    ;
    Vector2 mousePosition;
    cameraQuery.each([&](Camera2D &cam) { mousePosition = GetScreenToWorld2D(GetMousePosition(), cam); });
    Position p{ int(mousePosition.x), int(mousePosition.y) };
    if (IsMouseButtonPressed(0))
    {
      from = { (int)p.x, (int)p.y };
      pathfindingProcess();
    } 
    else if (IsMouseButtonPressed(1))
    {
      to = { (int)p.x, (int)p.y };
      pathfindingProcess();
    }

    BeginDrawing();
      ClearBackground(BLACK);
      cameraQuery.each([&](Camera2D &cam) { BeginMode2D(cam); });
        ecs.progress();
        //draw path
        
        if (from != IVec2{ -1, -1 })
        {
          Vector2 tileFromPos{ (from.x / (int)tile_size) * tile_size, (from.y / (int)tile_size) * tile_size };
          Rectangle fromRect{ tileFromPos.x, tileFromPos.y, tile_size, tile_size };
          DrawRectangleLinesEx(fromRect, 5, BLUE);
        }
        if (to != IVec2{ -1, -1 })
        {
          Vector2 tileToPos{ (to.x / (int)tile_size) * tile_size, (to.y / (int)tile_size) * tile_size };
          Rectangle toRect{ tileToPos.x, tileToPos.y, tile_size, tile_size };
          DrawRectangleLinesEx(toRect, 5, BLUE);
        }
        if (path.size() == 1 && path[0].connIdx == size_t(-1))
        {
          Vector2 tileFromPos{ (from.x / (int)tile_size) * tile_size, (from.y / (int)tile_size) * tile_size };
          Rectangle fromRect{ tileFromPos.x, tileFromPos.y, tile_size, tile_size };
          Vector2 tileToPos{ (to.x / (int)tile_size) * tile_size, (to.y / (int)tile_size) * tile_size };
          Rectangle toRect{ tileToPos.x, tileToPos.y, tile_size, tile_size };
          Vector2 fromCenter{ fromRect.x + fromRect.width * 0.5f, fromRect.y + fromRect.height * 0.5f };
          Vector2 toCenter{ toRect.x + toRect.width * 0.5f, toRect.y + toRect.height * 0.5f };
          DrawLineEx(fromCenter, toCenter, 2.f, RED);
          DrawText(TextFormat("%d", int(path[0].score)),
            (fromCenter.x + toCenter.x) * 0.5f,
            (fromCenter.y + toCenter.y) * 0.5f,
            16, WHITE);
        }
        else if (path.size() > 0)
        {
          pathfinderQuery.each([&](const DungeonData &dd, const DungeonPortals &dp)
          {
            Vector2 startCenter;
            {
              Vector2 tileFromPos{ (from.x / (int)tile_size) * tile_size, (from.y / (int)tile_size) * tile_size };
              Rectangle fromRect{ tileFromPos.x, tileFromPos.y, tile_size, tile_size };
              startCenter.x = fromRect.x + fromRect.width * 0.5f;
              startCenter.y = fromRect.y + fromRect.height * 0.5f;
            }
            for (const auto pc : path)
            {
              Vector2 endCenter;
              if (pc.connIdx != dp.portals.size())
              {
                const PathPortal &endPortal = dp.portals[pc.connIdx];
                endCenter.x = (endPortal.startX + endPortal.endX + 1) * tile_size * 0.5f;
                endCenter.y = (endPortal.startY + endPortal.endY + 1) * tile_size * 0.5f;
              }
              else
              {
                Vector2 tileToPos{ (to.x / (int)tile_size) * tile_size, (to.y / (int)tile_size) * tile_size };
                Rectangle toRect{ tileToPos.x, tileToPos.y, tile_size, tile_size };
                endCenter.x = toRect.x + toRect.width * 0.5f;
                endCenter.y = toRect.y + toRect.height * 0.5f;
              }
              DrawLineEx(startCenter, endCenter, 2.f, RED);
              DrawText(TextFormat("%d", int(pc.score)),
                (startCenter.x + endCenter.x) * 0.5f,
                (startCenter.y + endCenter.y) * 0.5f,
                16, WHITE);
              startCenter = endCenter;
            }
          });
          
        }
      EndMode2D();
      // Advance to next frame. Process submitted rendering primitives.
    EndDrawing();
  }

  CloseWindow();

  return 0;
}
