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

1. Ett nytt spel börjar i grävaren. Stå still och tryck **V** för att sträcka ut armen, dra skopan genom jorden och lyfta lasset. Den gröna markören visar var material flyttas. Tryck **I** för att tippa från den lyfta skopan. **Tab** byter fordon.
2. För att lasta dumpern med grävaren, parkera flaket inom räckhåll och sväng den lyfta skopan över flaket innan du trycker **I**. Håll in en musknapp och dra för att ta över hydrauliken. **G** växlar piltangenternas funktion. Vid en materialdepå kan **R** lasta dumpern direkt.
3. Öppna kartan med **9** och välj ett av de ljusblå vadställena. Backa dit med dumpern och tippa med **I**. Sänk bulldozerns blad med **V** och kör framåt för att schakta. **I** sprider jorden som bladet har samlat upp och höjer bladet när lasset är slut. Växla till utjämning med **G**, sänk bladet igen och kör över fyllningen. Ett sänkt blad röjer också träd.
4. Lasta terränglastbilen och passera den gula startportalen. Kör till den röda målportalen med minst 18 m³ kvar på flaket.

| Funktion | Tangentbord och mus | Xbox |
|---|---|---|
| Kör och styr | WASD | LT/RT och vänster spak |
| Kliv i eller ur, byt fordon | E, Tab | Y, styrkors höger |
| Kamera | Mus, C, scrollhjul | Höger spak, tryck på höger spak för kamerabyte |
| Hydraulik | Piltangenter, J/K/N/M eller håll in en musknapp och dra | Håll LB och använd höger spak. På grävaren väljer RB sticka och skopa. |
| Verktyg och funktionsgrupp | V, G | X, RB på övriga maskiner |
| Tippa, lasta vid depå | I, R | B, styrkors upp |
| Broms, återställ fordon | Mellanslag, Backsteg | A, styrkors ned |
| Karta | 9 | Visa-knappen |
| Spara och läs in | F5, F9 | |

| Maskin | Upp / ned | Vänster / höger |
|---|---|---|
| Volvo L120H hjullastare | Höj / sänk lastararmen | Vinkla skopan bakåt / tippa framåt |
| Cat D6 schaktmaskin | Höj / sänk bladet | |
| Dumper och lastbil | Höj / sänk flaket | |
| Grävmaskin | Höj / sänk bommen | Dra in / sträck ut stickan |
| Grävmaskin med skopa/sväng vald med **G** | Slut / öppna skopan | Sväng vänster / höger |

Hjullastaren lastar när du sänker skäret och kör in i en hög. Vinkla skopan bakåt för transport. Lyft och tippa den framåt över marken eller ett flak för att tömma den. Flaket på dumpern och lastbilen håller sin vinkel när du släpper piltangenten. **I** startar automatisk tippning.

Sänk schaktbladet med **↓** eller slå på markföljning med **V**, och kör sedan framåt med **W**. **G** växlar mellan schaktning och utjämning. Tryck **I** för att lägga ut materialet som ligger framför bladet. Om bladet blir fullt vid fortsatt schaktning lägger maskinen överskottet till höger.

För manuell grävning, sätt tänderna mot marken med bom och sticka. Dra sedan musen åt höger med höger knapp intryckt för att sluta skopan. Med tangentbordet gör du samma sak med **Skift+K**, eller med **uppåtpilen** när skopa/sväng är vald med **G**.

Med **C** växlar du mellan hytten och kameran utanför maskinen. Instrumentet i hytten visar fart, färdriktning och last. På grävaren och bulldozern visar arbetsinstrumentet skärets höjd mot marken. Ett negativt värde betyder att skäret ligger under markytan.

Tryck **E** för att kliva ur. Till fots följer kameran föraren i tredje person. Rör dig med **WASD** eller vänster spak, spring fortare med **Skift** eller tryck på vänster spak, och hoppa med **mellanslag** eller **A** på handkontrollen. Använd scrollhjulet för att ändra kamerans avstånd och tryck **E** igen när du står nära en hytt.

Grävarens grundreglage följer Volvo-grävaren i Farming Simulator 25.

| Håll in | Dra upp / ned | Dra vänster / höger |
|---|---|---|
| Vänster musknapp | Höj / sänk bommen | Sväng vänster / höger |
| Höger musknapp | Sträck ut / dra in stickan | Öppna / slut skopan |
| Båda musknapparna | Slut / öppna skopan | Luta skopan åt vänster / höger |

Med tangentbordet höjer och sänker **J/N** bommen, och **K/M** svänger åt höger och vänster. Håll **Skift** för att i stället styra stickan med **J/N** och sluta eller öppna skopan med **K/M**. På handkontroll använder du höger spak med **LB** för bom/sväng, **RB** för sticka/skopa eller båda för sidolutning.

På hjullastaren håller du vänster musknapp och drar uppåt eller nedåt för att röra lastararmen. Dra åt vänster för att vinkla skopan bakåt och åt höger för att tippa. Med höger musknapp kan du också vinkla skopan genom att dra uppåt eller nedåt. På schaktmaskinen styr valfri musknapp bladets höjd.

## Testa 4K och 120 fps

Standardprofilen sätter ett tak på 120 fps med TSR från 50 % intern upplösning och behåller högsta texturkvaliteten. För att testa med en bestämd bildyta, ange till exempel `-Fullscreen -ResX=3840 -ResY=2160 -ForceRes` på en 4K-skärm med minst 120 Hz. Startargumentet `-MudHighResolution` väljer i stället skrivbordets upplösning med 100 % intern renderingsskala.

**F6** växlar mellan ett vanligt fönster och kantlös helskärm på skrivbordets upplösning. I Windows Sandbox växlar **Ctrl+Alt+Pause/Break** helskärm för själva Sandbox-fönstret. **F7** väljer en profil med 67 % intern upplösning och lägre detaljinställningar. **F8** väljer 100 % intern upplösning med högsta texturkvalitet och 16× anisotropisk filtrering.

Lägg till `-MudBenchmark` vid start för att mäta 30 sekunder under upprepad tippning vid det södra vadet. Ramtiderna skrivs till `Saved/Benchmark.csv`. Jämför snitt och 99:e percentilen på samma dator med samma upplösning. För 120 fps motsvarar rambudgeten 8,33 ms.

På RTX 4070 Laptop gav standardprofilen med det aktuella vattenmaterialet 119,6 fps i snitt vid 3 200 × 2 000 och 9,20 ms vid 99:e percentilen. Före vattenuppdateringen gav en 4K-bildyta i fönster på samma dator 114,1 fps med 50 % intern upplösning och 119,0 fps med 44 %. Upprepa 4K-testet med `-Windowed -ResX=3840 -ResY=2160 -ForceRes -MudBenchmark -ExecCmds="sg.ResolutionQuality 44"`.

Fristående test av massbalans, last och bärighet kan köras med en C++17-kompilator.

```sh
c++ -std=c++17 -ISource/MudConstructionSimulator/Public tests/earthwork.cpp -o /tmp/mud-earthwork
/tmp/mud-earthwork
```

## Markfysik och testmiljö

Se [SOIL_SIMULATION.md](SOIL_SIMULATION.md) för arkitektur, testresultat, kvarvarande begränsningar och hur marklabbet startas.

## Highlands

Den nya världen är 2 × 2 km. Start, tester och resultat finns i [HIGHLANDS.md](HIGHLANDS.md).
