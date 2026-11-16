# Mud Construction Simulator

Ett Windows-spelprojekt i Unreal Engine 5.7. Uppdraget går ut på att bygga en passage genom dalen och leverera minst 18 m³ i terränglastbilen från den gula gården till den röda portalen. Gräv ur marken, hämta material från fem depåer och höj ett av de två vadställena. Skopa, flak och mark delar samma volymbokföring.

## Bygg och kör på Windows

På byggdatorn behövs Unreal Engine 5.7, MSVC för Visual Studio 2022, Windows SDK och .NET Framework 4.8 Developer Pack. För installeraren behövs även Inno Setup 6.3 eller senare. Byggskriptet använder Unreals app-lokala körbibliotek eller de redistribuerbara DLL-filerna från Visual Studio 2022. Speldatorn behöver den färdiga installeraren.

```powershell
# Generera Unreal-resurser och öppna projektet. Tryck Play i editorn.
.\tools\build-windows.ps1 -Launch

# Bygg och kontrollera att dalen, maskinerna och jordarbetet startar i Unreal.
.\tools\build-windows.ps1 -Target Smoke

# Bygg och starta den paketerade Win64-versionen.
.\tools\build-windows.ps1 -Target Game -Launch

# Bygg och starttesta spelet före paketering i Dist\.
.\tools\build-windows.ps1 -Target Installer
```

Använd `-EngineRoot` om Unreal ligger på en annan plats. Använd `-Inno` för att ange sökvägen till `ISCC.exe`. Projektets höjdkarta och övriga världsdata kommer från `world/42`. För att återskapa dem, kör `node tools/generate-world.mjs 42` med Node.js 24.

## Installera och uppdatera

Kör `MudConstructionSimulator-<version>-Setup.exe` och starta spelet från Start-menyn. Installationen ligger under `%LOCALAPPDATA%\Programs\Mud Construction Simulator`. Spel och app-lokala körbibliotek installeras för den aktuella användaren.

Spara med **F5**, stäng spelet och kör nästa versions installerare. Start-menyn pekar därefter på den nya versionen. Installeraren avvisar äldre versioner. Varje version har en egen programmapp, och avinstalleraren tar bort de installerade versionerna. Sparfilen ligger i `%LOCALAPPDATA%\MudConstructionSimulator\Profiles\Default\Valley.json` och finns kvar efter avinstallation.

Höj `ProjectVersion` i `Config/DefaultGame.ini` när du bygger en ny utgåva. Varje bygge använder en egen undermapp i `Dist`. Paketeringen kontrollerar innehållet och kör ett starttest på den paketerade Win64-filen innan installeraren skapas.

### Signera en utgåva som ska delas

Lokala testbyggen är osignerade. Använd ett betrott kodsigneringscertifikat och SDK-verktyget SignTool när du delar en version. Bygg med `-SignTool`, `-CertificateThumbprint` och `-TimestampUrl` för att signera spel, installerare och avinstallerare. Utgivningsfilen `*-release.json` innehåller version, SHA-256 för installeraren och SHA-256 för signeringscertifikatet. Förmedla certifikatets fingeravtryck genom en betrodd kanal så att mottagaren kan verifiera framtida uppdateringar mot samma utgivare.

```powershell
.\tools\install-verified-update.ps1 -Installer $installer -Sha256 $release.sha256 -PublisherCertificateSha256 $trustedPublisher
```

Skriptet kontrollerar filens hash, Windows-signaturen och det förväntade certifikatet innan installeraren startas. En signerad installation kräver en signerad uppdatering.

## Spela

1. Tryck **9** för kartan. De ljusblå markeringarna visar de två vadställena och det stora kartläget visar uppskattad fyllnadsvolym.
2. Kör dumpern till en grusdepå och stanna vid högen. **R** lastar från depån. För manuell lastning, ställ dumperns flak inom räckhåll för grävaren. Sänk skopan i materialet, aktivera grävning med **V**, lyft och sväng över flaket. Tryck **I** för att tömma skopan.
3. Backa dumpern mot vadet och tippa med **I**. Kör sakta medan du tippar för att lägga ut material över flera meter. Kör bulldozern över gruset med bladet nere och utjämning vald för att jämna ut körbanan. Ett sänkt blad röjer också träd.
4. Lasta terränglastbilen och passera den gula startportalen. Kör till den röda målportalen med minst 18 m³ kvar på flaket.

| Funktion | Tangentbord och mus | Xbox |
|---|---|---|
| Kör och styr | WASD | LT/RT och vänster spak |
| Kliv i eller ur, byt fordon | E, Tab | Y, styrkors höger |
| Kamera | Mus, C, scrollhjul | Höger spak, tryck på höger spak för kamerabyte |
| Hydraulik | J/K, N/M eller håll höger musknapp och dra | Håll LB och använd höger spak |
| Verktyg och funktionsgrupp | V, G | X, RB |
| Tippa, lasta vid depå | I, R | B, styrkors upp |
| Broms, återställ fordon | Mellanslag, Backsteg | A, styrkors ned |
| Karta | 9 | Visa-knappen |
| Spara och läs in | F5, F9 | |

Bulldozerns **G** växlar mellan schaktning och utjämning. På grävaren växlar knappen mellan bom/sticka och skopa/sväng. Kontrollerna kan redigeras i `Config/DefaultInput.ini` före ompaketering.

## Testa 4K och 120 fps

Ställ Windows-skrivbordet på 3 840 × 2 160 och minst 120 Hz. Växla till kantlös helskärm med **F6**. **F7** väljer 67 % intern upplösning med TSR och **F8** väljer 100 %. Båda profilerna har ett tak på 120 fps. Starta samma paketerade version med argumentet `-MudBenchmark` för att mäta 30 sekunder under upprepad tippning vid det södra vadet. Ramtiderna skrivs till `Saved/Benchmark.csv`. Jämför snitt och 99:e percentilen på samma dator med samma upplösning. För 120 fps motsvarar rambudgeten 8,33 ms.

Fristående test av massbalans, last och bärighet kan köras med en C++17-kompilator.

```sh
c++ -std=c++17 -ISource/MudConstructionSimulator/Public tests/earthwork.cpp -o /tmp/mud-earthwork
/tmp/mud-earthwork
```
