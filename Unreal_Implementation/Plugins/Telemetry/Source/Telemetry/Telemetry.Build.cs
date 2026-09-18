using UnrealBuildTool;
using System.IO;

public class Telemetry : ModuleRules
{
    public Telemetry(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "HeadMountedDisplay",
            "XRBase", // en versiones nuevas solo encuentra HeadMountedDisplay aqui
            "InputCore",
            "EnhancedInput"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { 
            "HeadMountedDisplay",
            "XRBase"
        });

        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            // Necesario para Android/AndroidApplication.h (GetJavaEnv, GetGameActivityThis, etc)
            PrivateDependencyModuleNames.Add("Launch");
            // 1) AAR (Java + permiso INTERNET + proguard keep), via APL
            string APL = System.IO.Path.Combine(ModuleDirectory, "Telemetry_APL.xml");
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", APL);
            // 2) .so �link-only�: el AAR pone el .so dentro del APK,pero el enlazador necesita verlo en build
            string LibForLink = System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "../ThirdParty/Android/arm64-v8a/libtelemetria.so"));
            if (System.IO.File.Exists(LibForLink))
            {
                PublicAdditionalLibraries.Add(LibForLink);
            }
            else
            {
                System.Console.WriteLine("WARNING: libtelemetria.so not found for link-only: " + LibForLink);
            }
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // telemetria.dll + telemetria.lib (MSVC-built import lib, NO MinGW
            // libtelemetria.dll.a) under Source/ThirdParty/Windows/{bin,lib}.
            // Ver cpp/README-build.md y Unreal_Implementation/README_Unreal_Windows.md
            string Third = System.IO.Path.Combine(ModuleDirectory, "../ThirdParty/Windows");
            string ImportLib = System.IO.Path.Combine(Third, "lib/telemetria.lib");
            string RuntimeDll = System.IO.Path.Combine(Third, "bin/telemetria.dll");
            // Mismo trato que la rama Android: avisar sin romper el arranque del módulo,
            // para que el fallo sea legible y no un LNK1181 sin contexto.
            if (System.IO.File.Exists(ImportLib))
            {
                PublicAdditionalLibraries.Add(ImportLib);
                PublicDelayLoadDLLs.Add("telemetria.dll");
            }
            else
            {
                System.Console.WriteLine("WARNING: telemetria.lib not found: " + ImportLib
                    + " -> build the Win64 library (cpp/README-build.md) and place it, with telemetria.dll, under "
                    + Third + "/{lib,bin}. Linking fails with unresolved telemetry_* until then.");
            }
            if (System.IO.File.Exists(RuntimeDll))
            {
                RuntimeDependencies.Add(RuntimeDll);
            }
            else
            {
                System.Console.WriteLine("WARNING: telemetria.dll not found: " + RuntimeDll
                    + " -> it must be staged next to the binary at runtime.");
            }
            // add `winhttp` is unnecessary: the DLL links it privately.
        }
    }
}
