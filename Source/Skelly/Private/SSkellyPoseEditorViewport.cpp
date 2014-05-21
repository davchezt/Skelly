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
#include "SSkellyPoseEditorViewport.h"
#include "SkellyPoseEditorViewportClient.h"
#include "SkellyPoseEditor.h"
#include "SSkellyPoseEditorViewportToolBar.h"

namespace Skelly {

void SPoseEditorViewport::Construct(const FArguments& InArgs, TSharedPtr<FPoseEditor> inPoseEditor)
{
	_poseEditorWeakPtr = inPoseEditor;

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SPoseEditorViewport::MakeEditorViewportClient()
{
	TSharedPtr<FPoseEditor> poseEditor = _poseEditorWeakPtr.Pin();
	_poseEditorViewportClient = MakeShareable(
		new FPoseEditorViewportClient(
			poseEditor.IsValid() ? &poseEditor->GetPreviewScene() : nullptr
		)
	);

	_poseEditorViewportClient->ViewportType = LVT_Perspective;
	_poseEditorViewportClient->bSetListenerPosition = false;
	_poseEditorViewportClient->SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
	_poseEditorViewportClient->SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);

	return _poseEditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SPoseEditorViewport::MakeViewportToolbar()
{
	return SNew(SPoseEditorViewportToolBar, SharedThis(this))
		.Cursor(EMouseCursor::Default);
}

void SPoseEditorViewport::BindCommands()
{
	SEditorViewport::BindCommands();
}

} // namespace Skelly
