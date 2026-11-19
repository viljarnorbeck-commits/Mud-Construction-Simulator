#pragma once
#include "CoreMinimal.h"
class AMudTerrain;
class AMudMachine;
bool RunSoilValidation(AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet);
