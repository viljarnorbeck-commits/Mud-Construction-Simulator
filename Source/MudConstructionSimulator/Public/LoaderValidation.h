#pragma once
#include "CoreMinimal.h"
class AMudTerrain;
class AMudMachine;
bool RunLoaderValidation(AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet);
