// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class AvatarProjectTarget : TargetRules
{
	public AvatarProjectTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
        bOverrideBuildEnvironment = true;
        bForceEnableExceptions = true;			// Avoid warnings about c++ exceptions

        ExtraModuleNames.AddRange( new string[] { "AvatarProject" } );
	}
}
