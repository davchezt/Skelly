//-------------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2014 Vadim Macagon
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//-------------------------------------------------------------------------------

#include "SkellyPrivatePCH.h"
#include "SkellyPoseEditor.h"
#include "WorkspaceMenuStructureModule.h"
#include "PoseAsset/SkellyPose.h"
#include "SSkellyPoseEditorViewport.h"
#include "SkellyPoseEditorViewportClient.h"
#include "SkellyPoseEditorViewportCommands.h"
#include "SSkellySkeletonTree.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "SkellyBone.h"
#include "AnimGraphDefinitions.h"

#define LOCTEXT_NAMESPACE "Skelly.PoseEditor"

namespace Skelly {

namespace PoseEditorTabID
{
	const FName Skeleton(TEXT("Skeleton"));
	const FName Viewport(TEXT("Viewport"));
	const FName Details(TEXT("Details"));
}

FName FPoseEditor::PoseEditorAppName("PoseEditorApp");

void FPoseEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& tabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(tabManager);

	auto group = WorkspaceMenu::GetMenuStructure().GetAssetEditorCategory();

	tabManager->RegisterTabSpawner(
		PoseEditorTabID::Skeleton, FOnSpawnTab::CreateSP(this, &FPoseEditor::OnSpawnSkeletonTab)
	)
	.SetDisplayName(_skeletonTabTitle)
	.SetGroup(group);

	tabManager->RegisterTabSpawner(
		PoseEditorTabID::Viewport, FOnSpawnTab::CreateSP(this, &FPoseEditor::OnSpawnViewportTab)
	)
	.SetDisplayName(_viewportTabTitle)
	.SetGroup(group);

	tabManager->RegisterTabSpawner(
		PoseEditorTabID::Details, FOnSpawnTab::CreateSP(this, &FPoseEditor::OnSpawnDetailsTab)
	)
	.SetDisplayName(_detailsTabTitle)
	.SetGroup(group);
}

void FPoseEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& tabManager)
{
	tabManager->UnregisterTabSpawner(PoseEditorTabID::Skeleton);
	tabManager->UnregisterTabSpawner(PoseEditorTabID::Viewport);
	tabManager->UnregisterTabSpawner(PoseEditorTabID::Details);

	FAssetEditorToolkit::UnregisterTabSpawners(tabManager);
}

FName FPoseEditor::GetToolkitFName() const
{
	return FName("SkellyPoseEditor");
}

FText FPoseEditor::GetBaseToolkitName() const
{
	return LOCTEXT("BaseToolkitName", "Pose Editor");
}

FString FPoseEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Pose ").ToString();
}

FLinearColor FPoseEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.0f, 0.0f, 0.2f, 0.5f);
}

void FPoseEditor::AddReferencedObjects(FReferenceCollector& collector)
{
	collector.AddReferencedObject(_currentPose);
}

FPoseEditor::FPoseEditor()
	: _currentPose(nullptr)
	, _skeletalMeshPreviewComponent(nullptr)
	, _skeletonTabTitle(LOCTEXT("SkeletonTab", "Skeleton"))
	, _viewportTabTitle(LOCTEXT("ViewportTab", "Viewport"))
	, _detailsTabTitle(LOCTEXT("DetailsTab", "Details"))
{
}

FPoseEditor::~FPoseEditor()
{
	if (_detailsViewBone)
	{
		_detailsViewBone->RemoveFromRoot();
	}

	if (_skeletalMeshPreviewComponent)
	{
		_skeletalMeshPreviewComponent->RemoveFromRoot();
	}

	GEditor->UnregisterForUndo(this);
}

void FPoseEditor::Startup()
{
	FPoseEditorViewportCommands::Register();
}

void FPoseEditor::Shutdown()
{
	FPoseEditorViewportCommands::Unregister();
}

void FPoseEditor::InitPoseEditor(
	EToolkitMode::Type toolkitMode, TSharedPtr<IToolkitHost>& editWithinLevelEditor,
	USkellyPose* poseToEdit
)
{
	check(poseToEdit);

	GEditor->RegisterForUndo(this);

	_currentPose = poseToEdit;
	_viewport = SNew(SPoseEditorViewport, SharedThis(this));
	_viewportClientWeakPtr = 
		StaticCastSharedPtr<FPoseEditorViewportClient>(_viewport->GetViewportClient());

	auto viewportClient = _viewportClientWeakPtr.Pin();
	if (viewportClient.IsValid())
	{
		viewportClient->OnSelectionChanged.BindSP(this, &FPoseEditor::Viewport_OnSelectionChanged);
	}
	
	FDetailsViewArgs detailsViewArgs;
	detailsViewArgs.bAllowSearch = true;
	detailsViewArgs.bLockable = false;
	detailsViewArgs.bUpdatesFromSelection = false;
	detailsViewArgs.bHideActorNameArea = true;
	detailsViewArgs.bObjectsUseNameArea = false;
	//detailsViewArgs.NotifyHook = this;

	auto& propertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(
		TEXT("PropertyEditor")
	);
	_detailsView = propertyEditorModule.CreateDetailView(detailsViewArgs);

	FAssetEditorToolkit::InitAssetEditor(
		toolkitMode, editWithinLevelEditor, PoseEditorAppName, GenerateDefaultStandaloneLayout(),
		true, true, poseToEdit
	);

	check(!_skeletalMeshPreviewComponent);

	_skeletalMeshPreviewComponent = NewObject<UDebugSkelMeshComponent>();
	// ensure the preview component is kept alive until this editor is destroyed
	_skeletalMeshPreviewComponent->AddToRoot();

	_detailsViewBone = NewObject<USkellyBone>();
	_detailsViewBone->AddToRoot();

	if (poseToEdit && poseToEdit->GetSkeleton())
	{
		if (_skeletonTree.IsValid())
		{
			_skeletonTree->Populate(poseToEdit->GetSkeleton()->GetReferenceSkeleton());
		}

		// find a suitable mesh for this skeleton (don't really care which)
		auto previewMesh = poseToEdit->GetSkeleton()->GetPreviewMesh(true);
		if (previewMesh)
		{
			SetPreviewSkeletalMesh(previewMesh);
		}
	}

	_skeletalMeshPreviewComponent->EnablePreview(true, nullptr, nullptr);
}

TSharedRef<FTabManager::FLayout> FPoseEditor::GenerateDefaultStandaloneLayout()
{
	return FTabManager::NewLayout("Standalone_SkellyPoseEditor_Layout")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split // toolbar at top of window
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(FAssetEditorToolkit::GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split // content area
			(
				FTabManager::NewSplitter()
				->SetSizeCoefficient(0.9f)
				->SetOrientation(Orient_Horizontal)
				->Split // skeleton tree
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->AddTab(PoseEditorTabID::Skeleton, ETabState::OpenedTab)
				)
				->Split // viewport
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.6f)
					->SetHideTabWell(true)
					->AddTab(PoseEditorTabID::Viewport, ETabState::OpenedTab)
				)
				->Split // details
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->AddTab(PoseEditorTabID::Details, ETabState::OpenedTab)
				)
			)
		);
}

TSharedRef<SDockTab> FPoseEditor::OnSpawnSkeletonTab(const FSpawnTabArgs& args)
{
	return SNew(SDockTab)
		.Label(_skeletonTabTitle)
		[
			SAssignNew(_skeletonTree, SSkeletonTree)
			.OnSelectionChanged(this, &FPoseEditor::SkeletonTree_OnSelectionChanged)
		];
}

TSharedRef<SDockTab> FPoseEditor::OnSpawnViewportTab(const FSpawnTabArgs& args)
{
	return SNew(SDockTab)
		.Label(_viewportTabTitle)
		[
			_viewport.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPoseEditor::OnSpawnDetailsTab(const FSpawnTabArgs& args)
{
	return SNew(SDockTab)
		.Label(_detailsTabTitle)
		[
			_detailsView.ToSharedRef()
		];
}

void FPoseEditor::SetPreviewSkeletalMesh(USkeletalMesh* inPreviewSkeletalMesh)
{
	check(_currentPose);
	check(inPreviewSkeletalMesh);

	if (_currentPose->GetSkeleton()->IsCompatibleMesh(inPreviewSkeletalMesh))
	{
		if (inPreviewSkeletalMesh != _skeletalMeshPreviewComponent->SkeletalMesh)
		{
			// FAssetEditorToolkit::GetToolkitName() will need to be overriden if there is more
			// than one object being edited (the pose is one of those, skeletal mesh is the other).
			// For now the pose editor doesn't deal with sockets, so no editing of the skeletal
			// mesh should be happening anyway.
			/*
			if (_skeletalMeshPreviewComponent->SkeletalMesh)
			{
				RemoveEditingObject(_skeletalMeshPreviewComponent->SkeletalMesh);
			}
			if (inPreviewSkeletalMesh)
			{
				AddEditingObject(inPreviewSkeletalMesh);
			}
			*/
			_skeletalMeshPreviewComponent->SetSkeletalMesh(inPreviewSkeletalMesh);

			_previewScene.AddComponent(_skeletalMeshPreviewComponent, FTransform::Identity);

			auto viewportClient = _viewportClientWeakPtr.Pin();
			if (viewportClient.IsValid())
			{
				viewportClient->SetSkeletalMeshPreviewComponent(
					_skeletalMeshPreviewComponent
				);
			}
			
		}
		else
		{
			_skeletalMeshPreviewComponent->InitAnim(true);
		}
	}
	else // mesh is incompatible with the current pose
	{
		// TODO: notify the user the mesh is incompatible
	}
}

void FPoseEditor::SetSelectedBoneNames(const TArray<FName>& inBoneNames)
{
	if (_skeletalMeshPreviewComponent)
	{
		// preallocate the array since the final size is already known
		_skeletalMeshPreviewComponent->BonesOfInterest.Empty(inBoneNames.Num());
		_skeletalMeshPreviewComponent->BonesOfInterest.AddUninitialized(inBoneNames.Num());

		for (int32 i = 0; i < inBoneNames.Num(); ++i)
		{
			_skeletalMeshPreviewComponent->BonesOfInterest[i] =
				_skeletalMeshPreviewComponent->GetBoneIndex(inBoneNames[i]);
		}
	}
}

void FPoseEditor::GetSelectedBoneNames(TArray<FName>& outBoneNames) const
{
	if (_skeletalMeshPreviewComponent)
	{
		outBoneNames.Empty(_skeletalMeshPreviewComponent->BonesOfInterest.Num());
		for (auto boneIndex : _skeletalMeshPreviewComponent->BonesOfInterest)
		{
			outBoneNames.Add(_skeletalMeshPreviewComponent->GetBoneName(boneIndex));
		}
	}
	else
	{
		outBoneNames.Empty();
	}
}

UObject* FPoseEditor::GetDetailsViewSourceObject()
{
	bool bSingleBoneSelected = 
		_skeletalMeshPreviewComponent && (_skeletalMeshPreviewComponent->BonesOfInterest.Num() == 1);
	
	if (bSingleBoneSelected)
	{
		auto boneIndex = _skeletalMeshPreviewComponent->BonesOfInterest[0];
		auto boneName = _skeletalMeshPreviewComponent->GetBoneName(boneIndex);
		_detailsViewBone->BoneName = boneName;
		_detailsViewBone->ParentBoneName = _skeletalMeshPreviewComponent->GetParentBone(boneName);
		auto* skeletalControl = 
			_skeletalMeshPreviewComponent->PreviewInstance->FindModifiedBone(boneName);

		// if the bone has been modified grab the transform from the skeletal control, 
		// otherwise use the local transform from the reference pose
		if (skeletalControl)
		{
			_detailsViewBone->BoneTransform.SetComponents(
				skeletalControl->Rotation.Quaternion(), skeletalControl->Translation, 
				skeletalControl->Scale
			);
		}
		else
		{
			_detailsViewBone->BoneTransform =
				_skeletalMeshPreviewComponent->SkeletalMesh->RefSkeleton.GetRefBonePose()[boneIndex];
		}
		return _detailsViewBone;
	}
	return nullptr;
}

void FPoseEditor::SkeletonTree_OnSelectionChanged()
{
	TArray<FName> boneNames;
	_skeletonTree->GetSelectedBoneNames(boneNames);
		
	SetSelectedBoneNames(boneNames);

	// force the viewport to redraw
	if (_viewport.IsValid())
	{
		_viewport->Refresh();
	}

	if (_detailsView.IsValid())
	{
		bool bForceRefresh = true;
		_detailsView->SetObject(GetDetailsViewSourceObject(), bForceRefresh);
	}
}

void FPoseEditor::Viewport_OnSelectionChanged()
{
	TArray<FName> boneNames;
	GetSelectedBoneNames(boneNames);
		
	if (_skeletonTree.IsValid())
	{
		_skeletonTree->SetSelectedBoneNames(boneNames);
	}

	if (_detailsView.IsValid())
	{
		bool bForceRefresh = true;
		_detailsView->SetObject(GetDetailsViewSourceObject(), bForceRefresh);
	}
}

void FPoseEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		FSlateApplication::Get().DismissAllMenus();

		if (_viewport.IsValid())
		{
			_viewport->Refresh();
		}
	}
}

void FPoseEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		FSlateApplication::Get().DismissAllMenus();

		if (_viewport.IsValid())
		{
			_viewport->Refresh();
		}
	}
}

} // namespace Skelly

#undef LOCTEXT_NAMESPACE
