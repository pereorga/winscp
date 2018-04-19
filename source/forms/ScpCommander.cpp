//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include "ScpCommander.h"

#include <Common.h>
#include <CoreMain.h>
#include <Interface.h>
#include <TextsCore.h>
#include <TextsWin.h>
#include <HelpWin.h>
#include <VCLCommon.h>
#include <GUITools.h>
#include <DragDrop.hpp>
#include <StrUtils.hpp>

#include "Glyphs.h"
#include "NonVisual.h"
#include "Tools.h"
#include "WinConfiguration.h"
#include "TerminalManager.h"
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma link "CustomDirView"
#pragma link "CustomScpExplorer"
#pragma link "CustomUnixDirView"
#pragma link "IEListView"
#pragma link "NortonLikeListView"
#pragma link "UnixDirView"
#pragma link "DirView"
#pragma link "PathLabel"
#pragma link "HistoryComboBox"
#pragma link "CustomDriveView"
#pragma link "DriveView"
#pragma link "UnixDriveView"
#pragma link "TB2Dock"
#pragma link "TB2Item"
#pragma link "TB2Toolbar"
#pragma link "TBX"
#pragma link "TB2ExtItems"
#pragma link "TBXExtItems"
#pragma link "TBXLists"
#pragma link "TBXStatusBars"
#pragma link "TBXToolPals"
#pragma link "ThemePageControl"
#ifndef NO_RESOURCES
#pragma resource "*.dfm"
#endif
//---------------------------------------------------------------------------
class TSynchronizedBrowsingGuard
{
public:
  TSynchronizedBrowsingGuard()
  {
    FWasSynchronisingBrowsing = NonVisualDataModule->SynchronizeBrowsingAction->Checked;
    NonVisualDataModule->SynchronizeBrowsingAction->Checked = false;
  }

  ~TSynchronizedBrowsingGuard()
  {
    NonVisualDataModule->SynchronizeBrowsingAction->Checked = FWasSynchronisingBrowsing;
  }

private:
  bool FWasSynchronisingBrowsing;
};
//---------------------------------------------------------------------------
__fastcall TScpCommanderForm::TScpCommanderForm(TComponent* Owner)
        : TCustomScpExplorerForm(Owner)
{
  FConstructed = true;
  FCurrentSide = osLocal;
  FLastLeftPanelWidth = LeftPanelWidth;
  FNormalPanelsWidth = -1;
  FSynchronisingBrowse = false;
  FFirstTerminal = true;
  FInternalDDDownloadList = new TStringList();
  FLocalPathComboBoxPaths = new TStringList();

  LocalPathComboUpdateDrives();

  LocalBackButton->LinkSubitems = HistoryMenu(osLocal, true)->Items;
  LocalForwardButton->LinkSubitems = HistoryMenu(osLocal, false)->Items;
  RemoteBackButton->LinkSubitems = HistoryMenu(osRemote, true)->Items;
  RemoteForwardButton->LinkSubitems = HistoryMenu(osRemote, false)->Items;

  TopDock->PopupMenu = NonVisualDataModule->CommanderBarPopup;
  CopyPopup(StatusBar, TopDock);
  CopyPopup(QueueDock, TopDock);
  CopyPopup(QueueLabel, TopDock);
  CopyPopup(BottomDock, TopDock);
  CopyPopup(QueueSeparatorPanel, TopDock);

  LocalTopDock->PopupMenu = NonVisualDataModule->LocalPanelPopup;
  CopyPopup(LocalPathLabel, LocalTopDock);
  CopyPopup(LocalStatusBar, LocalTopDock);
  CopyPopup(LocalDriveView, LocalTopDock);
  CopyPopup(LocalBottomDock, LocalTopDock);

  RemoteTopDock->PopupMenu = NonVisualDataModule->RemotePanelPopup;
  CopyPopup(RemotePathLabel, RemoteTopDock);
  CopyPopup(RemoteStatusBar, RemoteTopDock);
  CopyPopup(RemoteDriveView, RemoteTopDock);
  CopyPopup(RemoteBottomDock, RemoteTopDock);

  SetShortcuts();
  Splitter->ShowHint = True;
  LocalPanelSplitter->ShowHint = true;
  reinterpret_cast<TLabel*>(Splitter)->OnDblClick = SplitterDblClick;
  reinterpret_cast<TLabel*>(LocalPanelSplitter)->OnDblClick = PanelSplitterDblClick;
  reinterpret_cast<TLabel*>(RemotePanelSplitter)->OnDblClick = PanelSplitterDblClick;

  CommandLineCombo->Text = L"";
  FCommandLineComboPopulated = false;

  for (int i = 0; i < Toolbar2Toolbar->Items->Count; i++)
  {
    UpdateToolbar2ItemCaption(Toolbar2Toolbar->Items->Items[i]);
  }

  UseDesktopFont(LocalDirView);
  UseDesktopFont(LocalDriveView);
  UseDesktopFont(LocalPathLabel);
  UseDesktopFont(RemotePathLabel);
  UseDesktopFont(LocalStatusBar);
  UseDesktopFont(StatusBar);

  NonVisualDataModule->QueueSpeedComboBoxItem(QueueSpeedComboBoxItem);
  // particularly to reorder panels on right-to-left bidi mode
  ConfigurationChanged();
}
//---------------------------------------------------------------------------
__fastcall TScpCommanderForm::~TScpCommanderForm()
{
  delete FInternalDDDownloadList;
  SAFE_DESTROY(FLocalPathComboBoxPaths);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::UpdateToolbar2ItemCaption(TTBCustomItem * Item)
{
  Item->Caption =
    ShortCutToText(Item->ShortCut) + L" " +
    StripEllipsis(StripHotkey(Item->Caption));
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RestoreFormParams()
{
  DebugAssert(WinConfiguration);
  TCustomScpExplorerForm::RestoreFormParams();
  RestoreForm(WinConfiguration->ScpCommander.WindowParams, this);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RestoreParams()
{
  DebugAssert(Configuration);

  TCustomScpExplorerForm::RestoreParams();
  LeftPanelWidth = WinConfiguration->ScpCommander.LocalPanelWidth;
  LoadToolbarsLayoutStr(WinConfiguration->ScpCommander.ToolbarsLayout, WinConfiguration->ScpCommander.ToolbarsButtons);
  if (IsUWP())
  {
    UpdatesToolbar->Visible = false;
  }
  SessionsPageControl->Visible = WinConfiguration->ScpCommander.SessionsTabs;
  StatusBar->Visible = WinConfiguration->ScpCommander.StatusBar;

  #define RESTORE_PANEL_PARAMS(PANEL) \
    PANEL ## DirView->ColProperties->ParamsStr = WinConfiguration->ScpCommander.PANEL ## Panel.DirViewParams; \
    PANEL ## StatusBar->Visible = WinConfiguration->ScpCommander.PANEL ## Panel.StatusBar; \
    PANEL ## DriveView->Visible = WinConfiguration->ScpCommander.PANEL ## Panel.DriveView; \
    if (PANEL ## DriveView->Align == alTop) \
      PANEL ## DriveView->Height = LoadDimension(WinConfiguration->ScpCommander.PANEL ## Panel.DriveViewHeight, WinConfiguration->ScpCommander.PANEL ## Panel.DriveViewHeightPixelsPerInch, this); \
    else \
      PANEL ## DriveView->Width = LoadDimension(WinConfiguration->ScpCommander.PANEL ## Panel.DriveViewWidth, WinConfiguration->ScpCommander.PANEL ## Panel.DriveViewWidthPixelsPerInch, this)
  RESTORE_PANEL_PARAMS(Local);
  RESTORE_PANEL_PARAMS(Remote);
  #undef RESTORE_PANEL_PARAMS
  FPanelsRestored = true;

  // just to make sure
  LocalDirView->DirColProperties->ExtVisible = false;
  RemoteDirView->UnixColProperties->ExtVisible = false;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::StoreParams()
{
  DebugAssert(WinConfiguration);

  WinConfiguration->BeginUpdate();
  try
  {
    WinConfiguration->ScpCommander.ToolbarsLayout = GetToolbarsLayoutStr();
    WinConfiguration->ScpCommander.ToolbarsButtons = GetToolbarsButtonsStr();
    WinConfiguration->ScpCommander.LocalPanelWidth = LeftPanelWidth;
    WinConfiguration->ScpCommander.SessionsTabs = SessionsPageControl->Visible;
    WinConfiguration->ScpCommander.StatusBar = StatusBar->Visible;


    SaveCommandLine();

    WinConfiguration->ScpCommander.CurrentPanel = FCurrentSide;


    #define STORE_PANEL_PARAMS(PANEL) \
      WinConfiguration->ScpCommander.PANEL ## Panel.DirViewParams = PANEL ## DirView->ColProperties->ParamsStr; \
      WinConfiguration->ScpCommander.PANEL ## Panel.StatusBar = PANEL ## StatusBar->Visible; \
      WinConfiguration->ScpCommander.PANEL ## Panel.DriveView = PANEL ## DriveView->Visible; \
      if (PANEL ## DriveView->Align == alTop) \
      { \
        WinConfiguration->ScpCommander.PANEL ## Panel.DriveViewHeight = PANEL ## DriveView->Height; \
        WinConfiguration->ScpCommander.PANEL ## Panel.DriveViewHeightPixelsPerInch = GetControlPixelsPerInch(this); \
      } \
      else \
      { \
        WinConfiguration->ScpCommander.PANEL ## Panel.DriveViewWidth = PANEL ## DriveView->Width; \
        WinConfiguration->ScpCommander.PANEL ## Panel.DriveViewWidthPixelsPerInch = GetControlPixelsPerInch(this); \
      }
    STORE_PANEL_PARAMS(Local);
    STORE_PANEL_PARAMS(Remote);
    #undef RESTORE_PANEL_PARAMS



    WinConfiguration->ScpCommander.WindowParams = StoreForm(this);

    TCustomScpExplorerForm::StoreParams();
  }
  __finally
  {
    WinConfiguration->EndUpdate();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::UpdateTerminal(TTerminal * Terminal)
{
  TCustomScpExplorerForm::UpdateTerminal(Terminal);

  TManagedTerminal * ManagedTerminal = dynamic_cast<TManagedTerminal *>(Terminal);
  DebugAssert(ManagedTerminal != NULL);
  DebugAssert(LocalDirView != NULL);

  SAFE_DESTROY(ManagedTerminal->LocalExplorerState);

  if (WinConfiguration->PreservePanelState)
  {
    ManagedTerminal->LocalExplorerState = LocalDirView->SaveState();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::UpdateSessionData(TSessionData * Data)
{
  // Keep in sync with TSessionData::CopyStateData
  TCustomScpExplorerForm::UpdateSessionData(Data);

  DebugAssert(LocalDirView);
  Data->LocalDirectory = LocalDirView->PathName;
  Data->SynchronizeBrowsing = NonVisualDataModule->SynchronizeBrowsingAction->Checked;
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::InternalDDDownload(UnicodeString & TargetDirectory)
{
  DebugAssert(IsFileControl(FDDTargetControl, osLocal));

  bool Result = false;
  if (FDDTargetControl == LocalDirView)
  {
    if (LocalDirView->DropTarget)
    {
      // when drop target is not directory, it is probably file type, which have
      // associated drop handler (such as ZIP file in WinXP). in this case we
      // must leave drop handling to destination application.
      // ! this check is duplicated in LocalDirViewDDTargetHasDropHandler()
      // for shellex downloads
      if (LocalDirView->ItemIsDirectory(LocalDirView->DropTarget))
      {
        TargetDirectory = LocalDirView->ItemFullFileName(LocalDirView->DropTarget);
        Result = true;
      }
    }
    else
    {
      TargetDirectory = DefaultDownloadTargetDirectory();
      Result = true;
    }
  }
  else if (FDDTargetControl == LocalDriveView)
  {
    DebugAssert(LocalDriveView->DropTarget != NULL);
    TargetDirectory = LocalDriveView->NodePathName(LocalDriveView->DropTarget);
    Result = true;
  }
  else
  {
    DebugFail();
    Abort();
  }

  return Result;
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TScpCommanderForm::DefaultDownloadTargetDirectory()
{
  return IncludeTrailingBackslash(LocalDirView->Path);
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::CopyParamDialog(TTransferDirection Direction,
  TTransferType Type, bool Temp, TStrings * FileList, UnicodeString & TargetDirectory,
  TGUICopyParamType & CopyParam, bool Confirm, bool DragDrop, int Options)
{
  bool Result = false;
  // Temp means d&d here so far, may change in future!
  if (Temp && (Direction == tdToLocal) &&
      IsFileControl(FDDTargetControl, osLocal))
  {
    Result = InternalDDDownload(TargetDirectory);
    if (Result)
    {
      DebugAssert(FileList->Count > 0);
      FInternalDDDownloadList->Assign(FileList);
    }
  }
  else if (!Temp && TargetDirectory.IsEmpty())
  {
    if (Direction == tdToLocal)
    {
      TargetDirectory = DefaultDownloadTargetDirectory();
    }
    else
    {
      TargetDirectory = UnixIncludeTrailingBackslash(RemoteDirView->Path);
    }
  }

  if (!Result)
  {
    Result = TCustomScpExplorerForm::CopyParamDialog(Direction, Type, Temp,
      FileList, TargetDirectory, CopyParam, Confirm, DragDrop, Options);
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::DoShow()
{
  // Make sure the RemoteDirView is disabled (if not connected yet)
  // before the focusing below,
  // otherwise we disable the view while setting it focused
  // (UpdateControls gets calling within the SetFocus),
  // leading to VCL focus inconsistency wih Windows,
  // and the view [anothing actually] not getting focused after the session
  // is finally connected
  UpdateControls();

  // If we do not call SetFocus on any control before DoShow,
  // no control will get focused on Login dialog
  // (HACK seems like a bug in VCL)
  if (WinConfiguration->ScpCommander.CurrentPanel == osLocal)
  {
    LocalDirView->SetFocus();
    FRemoteDirViewWasFocused = false;
  }
  else
  {
    if (RemoteDirView->Enabled)
    {
      RemoteDirView->SetFocus();
    }
    else
    {
      LocalDirView->SetFocus();
    }
    FRemoteDirViewWasFocused = true;
  }

  TCustomScpExplorerForm::DoShow();
}
//---------------------------------------------------------------------------
Boolean __fastcall TScpCommanderForm::AllowedAction(TAction * Action, TActionAllowed Allowed)
{
  #define FLAG ((TActionFlag)(Action->Tag))
  return
    TCustomScpExplorerForm::AllowedAction(Action, Allowed) &&
    // always require Commander flag
    (FLAG & afCommander) &&
    // if action is execution or update, we don't require any other flag
    // if we check for shortcut, we require proper dirview to be selected
    ((Allowed != aaShortCut) ||
     ((FLAG & afLocal) && (FCurrentSide == osLocal)) ||
     ((FLAG & afRemote) && (FCurrentSide == osRemote))
    );
  #undef FLAG
}
//---------------------------------------------------------------------------
TCustomDirView * __fastcall TScpCommanderForm::DirView(TOperationSide Side)
{
  Side = GetSide(Side);
  if (Side == osLocal)
  {
    return LocalDirView;
  }
  else
  {
    return TCustomScpExplorerForm::DirView(Side);
  }
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::DirViewEnabled(TOperationSide Side)
{
  Side = GetSide(Side);
  if (Side == osLocal)
  {
    return true;
  }
  else
  {
    return TCustomScpExplorerForm::DirViewEnabled(Side);
  }
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::IsFileControl(TObject * Control,
  TOperationSide Side)
{
  return
    ((Side == osLocal) &&
     ((Control == LocalDirView) || (Control == LocalDriveView))) ||
    TCustomScpExplorerForm::IsFileControl(Control, Side);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::ReloadLocalDirectory(const UnicodeString Directory)
{
  if (Directory.IsEmpty() || SamePaths(Directory, LocalDirView->Path))
  {
    LocalDirView->ReloadDirectory();
    LocalDriveView->ValidateDirectory(LocalDriveView->Selected);
  }
  TCustomScpExplorerForm::ReloadLocalDirectory();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::BatchStart(void *& Storage)
{
  Storage = new bool;
  *static_cast<bool*>(Storage) = LocalDirView->WatchForChanges;
  LocalDirView->WatchForChanges = false;
  LocalDriveView->WatchDirectory = false;

  TCustomScpExplorerForm::BatchStart(Storage);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::BatchEnd(void * Storage)
{
  TCustomScpExplorerForm::BatchEnd(Storage);

  DebugAssert(Storage != NULL);

  LocalDirView->WatchForChanges = *static_cast<bool*>(Storage);
  LocalDriveView->WatchDirectory = LocalDirView->WatchForChanges;

  delete Storage;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::StartingDisconnected()
{
  TCustomScpExplorerForm::StartingDisconnected();

  LocalDefaultDirectory();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::TerminalChanged()
{
  NonVisualDataModule->SynchronizeBrowsingAction->Checked = false;

  TCustomScpExplorerForm::TerminalChanged();

  if (Terminal)
  {
    // we will load completelly different directory, so particularly
    // do not attempt to select previously selected directory
    LocalDirView->ContinueSession(false);

    TManagedTerminal * ManagedTerminal = dynamic_cast<TManagedTerminal *>(Terminal);
    DebugAssert(ManagedTerminal != NULL);

    // reset home directory
    LocalDirView->HomeDirectory = L"";

    if (FFirstTerminal || !WinConfiguration->ScpCommander.PreserveLocalDirectory)
    {
      UnicodeString LocalDirectory = ManagedTerminal->StateData->LocalDirectory;
      bool DocumentsDir = LocalDirectory.IsEmpty();

      if (!DocumentsDir)
      {
        try
        {
          LocalDirView->Path = LocalDirectory;
        }
        catch(Exception & E)
        {
          DocumentsDir = true;
          if (!Terminal->SessionData->UpdateDirectories)
          {
            Terminal->ShowExtendedException(&E);
          }
        }
      }

      if (DocumentsDir)
      {
        LocalDefaultDirectory();
      }
    }
    FFirstTerminal = false;

    if (WinConfiguration->DefaultDirIsHome &&
        !Terminal->SessionData->UpdateDirectories)
    {
      LocalDirView->HomeDirectory = ExpandFileName(Terminal->SessionData->LocalDirectory);
    }

    if (WinConfiguration->PreservePanelState &&
        !WinConfiguration->ScpCommander.PreserveLocalDirectory)
    {
      if (ManagedTerminal->LocalExplorerState != NULL)
      {
        LocalDirView->RestoreState(ManagedTerminal->LocalExplorerState);
      }
      else
      {
        LocalDirView->ClearState();
      }

      NonVisualDataModule->SynchronizeBrowsingAction->Checked = ManagedTerminal->StateData->SynchronizeBrowsing;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDefaultDirectory()
{
  try
  {
    LocalDirView->HomeDirectory = L"";
    LocalDirView->ExecuteHomeDirectory();
  }
  catch(Exception & E)
  {
    ShowExtendedException(NULL, &E);
    LocalDirView->Path = ExtractFilePath(Application->ExeName);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::ConfigurationChanged()
{
  TCustomScpExplorerForm::ConfigurationChanged();
  if (WinConfiguration->DefaultDirIsHome && Terminal &&
      !Terminal->SessionData->UpdateDirectories)
  {
    LocalDirView->HomeDirectory = ExpandFileName(Terminal->SessionData->LocalDirectory);
  }
  else
  {
    LocalDirView->HomeDirectory = L"";
  }
  LocalDirView->DimmHiddenFiles = WinConfiguration->DimmHiddenFiles;
  LocalDriveView->DimmHiddenDirs = WinConfiguration->DimmHiddenFiles;
  LocalDirView->ShowHiddenFiles = WinConfiguration->ShowHiddenFiles;
  LocalDirView->FormatSizeBytes = WinConfiguration->FormatSizeBytes;
  LocalDriveView->ShowHiddenDirs = WinConfiguration->ShowHiddenFiles;
  LocalDirView->ConfirmOverwrite = WinConfiguration->ConfirmOverwriting;
  LocalDriveView->ConfirmOverwrite = WinConfiguration->ConfirmOverwriting;

  LocalDirView->NortonLike = WinConfiguration->ScpCommander.NortonLikeMode;
  RemoteDirView->NortonLike = WinConfiguration->ScpCommander.NortonLikeMode;

  LocalDirView->NaturalOrderNumericalSorting = WinConfiguration->NaturalOrderNumericalSorting;

  if (LocalDirView->RowSelect != WinConfiguration->FullRowSelect)
  {
    LocalDirView->RowSelect = WinConfiguration->FullRowSelect;
    // selection is not redrawn automatically when RowSelect changes
    LocalDirView->Invalidate();
  }

  // See also LocalDirViewDDTargetHasDropHandler
  LocalDirView->DragDropFilesEx->ShellExtensions->DropHandler =
    !WinConfiguration->DDExtEnabled;
  LocalDriveView->DragDropFilesEx->ShellExtensions->DropHandler =
    !WinConfiguration->DDExtEnabled;

  if (Panel(true)->Left > Panel(false)->Left)
  {
    DisableAlign();
    try
    {
      int AWidth = ClientWidth;
      Panel(false)->Align = alClient;
      // In bidi mode it gets swapped to alRight, what does not work with the rest of this logic
      Splitter->Align = alLeft;
      Panel(true)->Align = alLeft;
      TControl * ControlsOrder[] =
        { Panel(true), Splitter, Panel(false) };
      SetHorizontalControlsOrder(ControlsOrder, LENOF(ControlsOrder));
      Panel(true)->TabOrder = 0;
      Panel(false)->TabOrder = 1;
      ClientWidth = AWidth;
      LeftPanelWidth = FLastLeftPanelWidth;
    }
    __finally
    {
      EnableAlign();
    }

    int LocalIndex = MenuToolbar->Items->IndexOf(LocalMenuButton);
    int RemoteIndex = MenuToolbar->Items->IndexOf(RemoteMenuButton);
    MenuToolbar->Items->Move(LocalIndex, RemoteIndex);
    RemoteIndex = MenuToolbar->Items->IndexOf(RemoteMenuButton);
    MenuToolbar->Items->Move(RemoteIndex, LocalIndex);
    SWAP(TShortCut, NonVisualDataModule->LocalChangePathAction->ShortCut,
      NonVisualDataModule->RemoteChangePathAction->ShortCut);
  }

  if ((RemoteDriveView->Align == alLeft) != WinConfiguration->ScpCommander.TreeOnLeft)
  {
    TScpCommanderPanelConfiguration LocalPanel = WinConfiguration->ScpCommander.LocalPanel;
    TScpCommanderPanelConfiguration RemotePanel = WinConfiguration->ScpCommander.RemotePanel;

    bool TreeOnLeft = WinConfiguration->ScpCommander.TreeOnLeft;

    // save value only is it was set yet
    if (FPanelsRestored)
    {
      if (TreeOnLeft)
      {
        // want to be on left, so it is on top, saving height
        LocalPanel.DriveViewHeight = LocalDriveView->Height;
        RemotePanel.DriveViewHeight = RemoteDriveView->Height;
      }
      else
      {
        LocalPanel.DriveViewWidth = LocalDriveView->Width;
        RemotePanel.DriveViewWidth = RemoteDriveView->Width;
      }
    }

    TAlign NonClientAlign = (TreeOnLeft ? alLeft : alTop);
    // VCL adjusts cursors only between crVSplit and crHSplit,
    // See TSplitter.RequestAlign
    TCursor SplitterCursor = (TreeOnLeft ? crSizeWE : crSizeNS);
    LocalDriveView->Align = NonClientAlign;
    LocalPanelSplitter->Align = NonClientAlign;
    LocalPanelSplitter->Cursor = SplitterCursor;
    RemoteDriveView->Align = NonClientAlign;
    RemotePanelSplitter->Align = NonClientAlign;
    RemotePanelSplitter->Cursor = SplitterCursor;
    FixControlsPlacement();

    if (TreeOnLeft)
    {
      LocalDriveView->Width = LocalPanel.DriveViewWidth;
      RemoteDriveView->Width = RemotePanel.DriveViewWidth;
    }
    else
    {
      LocalDriveView->Height = LocalPanel.DriveViewHeight;
      RemoteDriveView->Height = RemotePanel.DriveViewHeight;
    }

    // in case it trigges config-changed event (does not),
    // make sure it does only after we apply the TreeOnLeft change to avoid endless recursion
    WinConfiguration->ScpCommander.LocalPanel = LocalPanel;
    WinConfiguration->ScpCommander.RemotePanel = RemotePanel;
  }

  if (FExplorerKeyboardShortcuts != WinConfiguration->ScpCommander.ExplorerKeyboardShortcuts)
  {
    SetShortcuts();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SetShortcuts()
{
  // set common norton shorcuts to our actions
  NonVisualDataModule->CommanderShortcuts();
  FExplorerKeyboardShortcuts = WinConfiguration->ScpCommander.ExplorerKeyboardShortcuts;
}
//---------------------------------------------------------------------------
TPanel * __fastcall TScpCommanderForm::Panel(bool Left)
{
  bool SwappedPanels = WinConfiguration->ScpCommander.SwappedPanels;
  if (IsRightToLeft())
  {
    SwappedPanels = !SwappedPanels;
  }
  return (SwappedPanels == Left ? RemotePanel : LocalPanel);
}
//---------------------------------------------------------------------------
TPanel * __fastcall TScpCommanderForm::CurrentPanel()
{
  return (FCurrentSide == osLocal ? LocalPanel : RemotePanel);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SetLeftPanelWidth(double value)
{
  double Total = LocalPanel->Width + RemotePanel->Width;
  FLeftPanelWidth = value;
  if (value * Total != Panel(true)->Width)
  {
    Panel(true)->Width = static_cast<int>(value * Total);
    UpdateControls();
  }
}
//---------------------------------------------------------------------------
double __fastcall TScpCommanderForm::GetLeftPanelWidth()
{
  return FLeftPanelWidth;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SplitterMoved(TObject * /*Sender*/)
{
  double Left = Panel(true)->Width;
  double Total = LocalPanel->Width + RemotePanel->Width;
  FLeftPanelWidth = Left / Total;
  FLastLeftPanelWidth = LeftPanelWidth;
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SplitterCanResize(TObject * /*Sender*/,
      int &NewSize, bool & /*Accept*/)
{
  // When splitter is drag so far to right, that width contraint of remote panel would
  // be violated, it doesn't stop, but extend form width.
  // Following prevents this behavior.
  if (ClientWidth - NewSize - Splitter->Width < Panel(false)->Constraints->MinWidth)
    NewSize = (ClientWidth - Panel(false)->Constraints->MinWidth - Splitter->Width);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SplitterDblClick(TObject * /*Sender*/)
{
  LeftPanelWidth = 0.5;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::PanelSplitterDblClick(TObject * Sender)
{
  TSplitter * Splitter = dynamic_cast<TSplitter *>(Sender);
  DebugAssert(Splitter != NULL);
  TCustomDriveView * DriveView;
  TCustomDriveView * OtherDriveView;
  if (Splitter == LocalPanelSplitter)
  {
    DriveView = LocalDriveView;
    OtherDriveView = RemoteDriveView;
  }
  else
  {
    DriveView = RemoteDriveView;
    OtherDriveView = LocalDriveView;
  }

  bool TreeOnLeft = WinConfiguration->ScpCommander.TreeOnLeft;
  DebugAssert(DriveView->Visible);
  if (OtherDriveView->Visible)
  {
    if (TreeOnLeft)
    {
      DriveView->Width = OtherDriveView->Width;
    }
    else
    {
      DriveView->Height = OtherDriveView->Height;
    }
  }
  else
  {
    if (TreeOnLeft)
    {
      OtherDriveView->Width = DriveView->Width;
    }
    else
    {
      OtherDriveView->Height = DriveView->Height;
    }
    OtherDriveView->Visible = true;
  }

  FixControlsPlacement();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::UpdateControls()
{
  TCustomScpExplorerForm::UpdateControls();

  UnicodeString SplitterLongHint = Splitter->Hint;
  int P = SplitterLongHint.Pos(L"|");
  if (P == 0)
  {
    P = SplitterLongHint.Pos(L"\n");
  }
  SplitterLongHint.Delete(1, P);
  Splitter->Hint = FORMAT(L"%0.0f%%\n%s", (LeftPanelWidth*100, SplitterLongHint));
  UnicodeString ACommandLinePromptLabel = LoadStr(COMMAND_LINE_LABEL) + " " +
    ((FCurrentSide == osRemote) ? L"$" : L">");
  if (CommandLinePromptLabel->Caption != ACommandLinePromptLabel)
  {
    CommandLinePromptLabel->Caption = ACommandLinePromptLabel;
    // command line combo width needs to be updated as caption width has probably changed
    ToolBarResize(CommandLineToolbar);
  }
  LocalDirView->Color = (SessionColor != 0 ? SessionColor : clWindow);
  LocalDriveView->Color = LocalDirView->Color;

  bool LocalSide = (FCurrentSide == osLocal);
  TAction * CurrentCopyAction = LocalSide ? NonVisualDataModule->LocalCopyAction : NonVisualDataModule->RemoteCopyAction;
  if (CurrentCopyItem->Action != CurrentCopyAction)
  {
    CurrentCopyItem->Action = CurrentCopyAction;
    CurrentCopyToolbar2Item->Action = CurrentCopyAction;
    UpdateToolbar2ItemCaption(CurrentCopyToolbar2Item);

    CurrentCopyNonQueueItem->Action = LocalSide ? NonVisualDataModule->LocalCopyNonQueueAction : NonVisualDataModule->RemoteCopyNonQueueAction;
    CurrentCopyQueueItem->Action = LocalSide ? NonVisualDataModule->LocalCopyQueueAction : NonVisualDataModule->RemoteCopyQueueAction;

    TAction * CurrentMoveAction = LocalSide ? NonVisualDataModule->LocalMoveAction : NonVisualDataModule->RemoteMoveAction;
    CurrentMoveItem->Action = CurrentMoveAction;
    CurrentMoveToolbar2Item->Action = CurrentMoveAction;
    UpdateToolbar2ItemCaption(CurrentMoveToolbar2Item);
  }

  CommandLineCombo->Enabled = (FCurrentSide == osLocal) || CanConsole();
  CommandLinePromptLabel->Enabled = CommandLineCombo->Enabled;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::ChangePath(TOperationSide Side)
{
  DebugAssert((Side == osLocal) || (Side == osRemote));
  TTBXComboBoxItem * PathComboBox;
  if (Side == osLocal)
  {
    PathComboBox = LocalPathComboBox;
  }
  else
  {
    PathComboBox = RemotePathComboBox;
  }
  ClickToolbarItem(PathComboBox, false);
}
//---------------------------------------------------------------------------
TControl * __fastcall TScpCommanderForm::GetComponent(Byte Component)
{
  switch (Component) {
    case fcToolBar2: return Toolbar2Toolbar; // name changed to enforce change of default visibility
    case fcStatusBar: return StatusBar;
    case fcLocalStatusBar: return LocalStatusBar;
    case fcRemoteStatusBar: return RemoteStatusBar;
    case fcCommandLinePanel: return CommandLineToolbar;
    case fcLocalTree: return LocalDriveView;
    case fcSessionToolbar: return SessionToolbar;
    case fcCustomCommandsBand: return CustomCommandsToolbar;
    case fcColorMenu: return reinterpret_cast<TControl*>(ColorMenuItem);
    case fcTransferDropDown: return reinterpret_cast<TControl*>(TransferDropDown);
    case fcTransferList: return reinterpret_cast<TControl*>(TransferList);
    case fcTransferLabel: return reinterpret_cast<TControl*>(TransferLabel);
    case fcLocalPopup: return reinterpret_cast<TControl *>(NonVisualDataModule->LocalFilePopup);
    case fcRemotePathComboBox: return reinterpret_cast<TControl*>(RemotePathComboBox);

    case fcCommanderMenuBand: return MenuToolbar;
    case fcCommanderSessionBand: return SessionToolbar;
    case fcCommanderPreferencesBand: return PreferencesToolbar;
    case fcCommanderSortBand: return SortToolbar;
    case fcCommanderCommandsBand: return CommandsToolbar;
    case fcCommanderUpdatesBand: return UpdatesToolbar;
    case fcCommanderTransferBand: return TransferToolbar;
    case fcCommanderCustomCommandsBand: return CustomCommandsToolbar;
    case fcCommanderLocalHistoryBand: return LocalHistoryToolbar;
    case fcCommanderLocalNavigationBand: return LocalNavigationToolbar;
    case fcCommanderLocalFileBand: return LocalFileToolbar;
    case fcCommanderLocalSelectionBand: return LocalSelectionToolbar;
    case fcCommanderRemoteHistoryBand: return RemoteHistoryToolbar;
    case fcCommanderRemoteNavigationBand: return RemoteNavigationToolbar;
    case fcCommanderRemoteFileBand: return RemoteFileToolbar;
    case fcCommanderRemoteSelectionBand: return RemoteSelectionToolbar;
    default: return TCustomScpExplorerForm::GetComponent(Component);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::FixControlsPlacement()
{
  TCustomScpExplorerForm::FixControlsPlacement();

  QueueSeparatorPanel->Visible = QueuePanel->Visible;

  LocalPanelSplitter->Visible = LocalDriveView->Visible;

  TControl * ControlsOrder[] =
    { BottomDock, QueueSeparatorPanel, QueueSplitter, QueuePanel, StatusBar };
  SetVerticalControlsOrder(ControlsOrder, LENOF(ControlsOrder));

  TControl * LocalControlsOrder[] =
    { LocalTopDock, LocalPathLabel, LocalDriveView, LocalPanelSplitter,
      LocalDirView, LocalBottomDock, LocalStatusBar };
  SetVerticalControlsOrder(LocalControlsOrder, LENOF(LocalControlsOrder));
  SetHorizontalControlsOrder(LocalControlsOrder, LENOF(LocalControlsOrder));

  TControl * RemoteControlsOrder[] =
    { RemoteTopDock, RemotePathLabel, RemoteDriveView, RemotePanelSplitter,
      RemoteDirView, RemoteBottomDock, RemoteStatusBar };
  SetVerticalControlsOrder(RemoteControlsOrder, LENOF(RemoteControlsOrder));
  SetHorizontalControlsOrder(RemoteControlsOrder, LENOF(RemoteControlsOrder));

  if (LocalDirView->ItemFocused != NULL)
  {
    LocalDirView->ItemFocused->MakeVisible(false);
  }
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::GetHasDirView(TOperationSide Side)
{
  return TCustomScpExplorerForm::GetHasDirView(Side) || (Side == osLocal);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::CompareDirectories()
{
  LocalDirView->CompareFiles(RemoteDirView, false,
    WinConfiguration->ScpCommander.CompareCriterias());
  RemoteDirView->CompareFiles(LocalDirView, false,
    WinConfiguration->ScpCommander.CompareCriterias());
  if (LocalDirView->SelCount + RemoteDirView->SelCount == 0)
  {
    MessageDialog(LoadStr(COMPARE_NO_DIFFERENCES), qtInformation, qaOK,
      HELP_COMPARE_NO_DIFFERENCES);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SynchronizeDirectories()
{
  UnicodeString LocalDirectory = LocalDirView->PathName;
  UnicodeString RemoteDirectory = RemoteDirView->PathName;
  DoSynchronizeDirectories(LocalDirectory, RemoteDirectory, false);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::FullSynchronizeDirectories()
{
  UnicodeString LocalDirectory = LocalDirView->PathName;
  UnicodeString RemoteDirectory = RemoteDirView->PathName;
  bool SaveMode = !(GUIConfiguration->SynchronizeModeAuto < 0);
  TSynchronizeMode Mode =
    (SaveMode ? (TSynchronizeMode)GUIConfiguration->SynchronizeModeAuto :
      ((FCurrentSide == osLocal) ? smRemote : smLocal));
  if (DoFullSynchronizeDirectories(LocalDirectory, RemoteDirectory, Mode,
        SaveMode, false))
  {
    if (SaveMode)
    {
      GUIConfiguration->SynchronizeModeAuto = Mode;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::ExploreLocalDirectory()
{
  OpenFolderInExplorer(LocalDirView->Path);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDirViewExecFile(TObject *Sender,
      TListItem *Item, bool &AllowExec)
{
  DebugAssert(Item);
  if ((UpperCase(PFileRec(Item->Data)->FileExt) == L"LNK") &&
      DirectoryExists(ApiPath(ResolveFileShortCut(LocalDirView->ItemFullFileName(Item), true))))
  {
    AllowExec = true;
  }
  else
  {
    DoDirViewExecFile(Sender, Item, AllowExec);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalFileControlDDDragEnter(TObject *Sender,
      IDataObject *DataObj, int grfKeyState, TPoint &Point, int &dwEffect,
      bool &Accept)
{
  // LocalDirViewDDDragEnter is duplication of
  // TCustomScpExplorerForm::DirViewDDDragEnter, but it differs in
  // literal type of 'DataObj' parameter.Actual type is however same
  FileControlDDDragEnter(Sender, DataObj, grfKeyState, Point, dwEffect, Accept);
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::PanelOperation(TOperationSide Side,
  bool DragDrop)
{
  return TCustomScpExplorerForm::PanelOperation(Side, DragDrop) ||
    (DropSourceControl == LocalDirView);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::FileOperationProgress(
  TFileOperationProgressType & ProgressData)
{
  // Heuristic: When operation finishes and DD targed is local dir view,
  // we suppose that drag&drop download finished, so local dir view should be
  // reloaded
  if (!ProgressData.InProgress && FProgressForm &&
      IsFileControl(FDDTargetControl, osLocal) &&
      ((ProgressData.Operation == ::foCopy) || (ProgressData.Operation == ::foMove)))
  {
    ReloadLocalDirectory();
  }
  TCustomScpExplorerForm::FileOperationProgress(ProgressData);
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TScpCommanderForm::ChangeFilePath(UnicodeString Path, TOperationSide Side)
{
  TGUICopyParamType CopyParams = GUIConfiguration->CurrentCopyParam;
  UnicodeString Result;
  while (!Path.IsEmpty())
  {
    int P = Path.Pos(Side == osLocal ? L'\\' : L'/');
    if (P > 0)
    {
      Result += Terminal->ChangeFileName(&CopyParams, Path.SubString(1, P - 1), Side, false) +
        (Side == osLocal ? L'/' : L'\\');
      Path.Delete(1, P);
    }
    else
    {
      Result += Terminal->ChangeFileName(&CopyParams, Path, osLocal, false);
      Path = L"";
    }
  }

  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::CreateRemoteDirectory(const UnicodeString & Path)
{
  UnicodeString Dir = UnixExtractFileDir(Path);
  if (!IsUnixRootPath(Dir) && !Terminal->FileExists(Dir))
  {
    CreateRemoteDirectory(Dir);
  }
  TRemoteProperties Properties = GUIConfiguration->NewDirectoryProperties;
  RemoteDirView->CreateDirectoryEx(Path, &Properties);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SynchronizeBrowsingLocal(
  UnicodeString PrevPath, UnicodeString & NewPath, bool Create)
{
  Terminal->ExceptionOnFail = true;
  TStrings * Paths = new TStringList();
  try
  {
    Paths->Add(IncludeTrailingBackslash(PrevPath));
    Paths->Add(IncludeTrailingBackslash(LocalDirView->Path));
    UnicodeString CommonPath;
    if (ExtractCommonPath(Paths, CommonPath))
    {
      PrevPath = IncludeTrailingBackslash(PrevPath);
      CommonPath = IncludeTrailingBackslash(CommonPath);
      NewPath = RemoteDirView->Path;
      while (!SamePaths(PrevPath, CommonPath))
      {
        if (NewPath == UnixExcludeTrailingBackslash(NewPath))
        {
          Abort();
        }
        NewPath = UnixExtractFilePath(UnixExcludeTrailingBackslash(NewPath));
        PrevPath = ExtractFilePath(ExcludeTrailingBackslash(PrevPath));
      }

      NewPath = UnixIncludeTrailingBackslash(NewPath) +
        ToUnixPath(LocalDirView->Path.SubString(PrevPath.Length() + 1,
          LocalDirView->Path.Length() - PrevPath.Length()));
    }
    else
    {
      Abort();
    }

    if (Create)
    {
      CreateRemoteDirectory(UnixExcludeTrailingBackslash(NewPath));
    }

    RemoteDirView->Path = NewPath;
  }
  __finally
  {
    Terminal->ExceptionOnFail = false;
    delete Paths;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::CreateLocalDirectory(const UnicodeString & Path)
{
  UnicodeString Dir = ExtractFileDir(Path);
  if (!Dir.IsEmpty() && !DirectoryExists(Dir))
  {
    CreateLocalDirectory(Dir);
  }
  LocalDirView->CreateDirectory(Path);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SynchronizeBrowsingRemote(
  UnicodeString PrevPath, UnicodeString & NewPath, bool Create)
{
  TStrings * Paths = new TStringList();
  try
  {
    Paths->Add(UnixIncludeTrailingBackslash(PrevPath));
    Paths->Add(UnixIncludeTrailingBackslash(RemoteDirView->Path));
    UnicodeString CommonPath;

    if (UnixExtractCommonPath(Paths, CommonPath))
    {
      PrevPath = UnixIncludeTrailingBackslash(PrevPath);
      CommonPath = UnixIncludeTrailingBackslash(CommonPath);

      UnicodeString NewLocalPath;
      NewPath = ExcludeTrailingBackslash(LocalDirView->Path);
      while (!UnixSamePath(PrevPath, CommonPath))
      {
        NewLocalPath = ExcludeTrailingBackslash(ExtractFileDir(NewPath));
        if (NewLocalPath == NewPath)
        {
          Abort();
        }
        NewPath = NewLocalPath;
        PrevPath = UnixExtractFilePath(UnixExcludeTrailingBackslash(PrevPath));
      }

      NewPath = IncludeTrailingBackslash(NewPath) +
        ChangeFilePath(
          RemoteDirView->Path.SubString(PrevPath.Length() + 1,
            RemoteDirView->Path.Length() - PrevPath.Length()),
          osRemote);
    }
    else
    {
      Abort();
    }
  }
  __finally
  {
    delete Paths;
  }

  if (Create)
  {
    CreateLocalDirectory(ExcludeTrailingBackslash(NewPath));
  }

  LocalDirView->Path = NewPath;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SynchronizeBrowsing(TCustomDirView * ADirView,
  UnicodeString PrevPath, UnicodeString & NewPath, bool Create)
{
  if (ADirView == LocalDirView)
  {
    SynchronizeBrowsingLocal(PrevPath, NewPath, Create);
  }
  else
  {
    SynchronizeBrowsingRemote(PrevPath, NewPath, Create);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SynchronizeBrowsing(TCustomDirView * ADirView)
{
  UnicodeString PrevPath;
  // cannot assign to UnicodeString before the class is constructed,
  // if we do, the value get lost when UnicodeString constructor gets finally called,
  // what results in memory leak in the best case
  if (FConstructed)
  {
    PrevPath = FPrevPath[ADirView == LocalDirView];
    FPrevPath[ADirView == LocalDirView] = ADirView->Path;
  }

  if (!FSynchronisingBrowse && NonVisualDataModule->SynchronizeBrowsingAction->Checked &&
      !PrevPath.IsEmpty() && PrevPath != ADirView->Path)
  {
    TValueRestorer<bool> AllowTransferPresetAutoSelectRestorer(FAllowTransferPresetAutoSelect);
    FAllowTransferPresetAutoSelect = false;
    TValueRestorer<bool> SynchronisingBrowseRestorer(FSynchronisingBrowse);
    FSynchronisingBrowse = true;

    try
    {
      UnicodeString NewPath;

      bool Error = false;
      std::unique_ptr<TStrings> ErrorMoreMessages;
      UnicodeString ErrorHelpKeyword;

      try
      {
        SynchronizeBrowsing(ADirView, PrevPath, NewPath, false);
      }
      // EAbort means that we do not know how to synchronize browsing
      // there's no fallback scenario for that
      catch(EAbort &)
      {
        throw;
      }
      catch(Exception & E)
      {
        // what does this say?
        if (Application->Terminated)
        {
          throw;
        }
        else
        {
          Error = true;
          ErrorMoreMessages.reset(ExceptionToMoreMessages(&E));
          ErrorHelpKeyword =
            MergeHelpKeyword(HELP_SYNC_DIR_BROWSE_ERROR, GetExceptionHelpKeyword(&E));
        }
      }

      // this was moved here out of the above catch clause,
      // to avoid deep nesting, what seems to cause some stray access violations
      if (Error)
      {
        if (MoreMessageDialog(FMTLOAD(SYNC_DIR_BROWSE_CREATE2, (NewPath)),
              ErrorMoreMessages.get(), qtConfirmation, qaYes | qaNo,
              ErrorHelpKeyword) == qaYes)
        {
          try
          {
            SynchronizeBrowsing(ADirView, PrevPath, NewPath, true);
          }
          catch(Exception & E)
          {
            if (!Application->Terminated)
            {
              Terminal->ShowExtendedException(&E);
            }
            throw;
          }
        }
        else
        {
          NonVisualDataModule->SynchronizeBrowsingAction->Checked = false;
        }
      }

    }
    catch(Exception & E)
    {
      NonVisualDataModule->SynchronizeBrowsingAction->Checked = false;
      // what does this say?
      if (Application->Terminated)
      {
        throw;
      }
      else
      {
        MessageDialog(LoadStr(SYNC_DIR_BROWSE_ERROR), qtInformation, qaOK,
          HELP_SYNC_DIR_BROWSE_ERROR);
      }
    }

    // note the value restorers at the beginning of this block
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::DoDirViewLoaded(TCustomDirView * ADirView)
{
  TCustomScpExplorerForm::DoDirViewLoaded(ADirView);

  UpdateControls();

  SynchronizeBrowsing(ADirView);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::AddEditLink(TOperationSide Side, bool Add)
{
  if (GetSide(Side) == osLocal)
  {
    bool Edit = false;
    UnicodeString FileName;
    UnicodeString PointTo;
    bool SymbolicLink = true;

    if (LocalDirView->ItemFocused)
    {
      DebugAssert(LocalDirView->ItemFocused->Data);
      PFileRec FileRec = (PFileRec)LocalDirView->ItemFocused->Data;

      Edit = !Add && (UpperCase(FileRec->FileExt) == L"LNK");
      if (Edit)
      {
        UnicodeString FullName = LocalDirView->ItemFullFileName(LocalDirView->ItemFocused);
        FileName = FullName;
        PointTo = ResolveFileShortCut(FullName, false);
        if (PointTo.IsEmpty())
        {
          throw Exception(FMTLOAD(RESOLVE_SHORTCUT_ERROR, (FullName)));
        }
      }
      else
      {
        PointTo = FileRec->FileName;
      }
    }

    if (DoSymlinkDialog(FileName, PointTo, osLocal, SymbolicLink, Edit, false))
    {
      Configuration->Usage->Inc(L"LocalShortcutsCreated");

      DebugAssert(SymbolicLink);
      DebugAssert(!FileName.IsEmpty());
      DebugAssert(!PointTo.IsEmpty());

      if (ExtractFileDrive(FileName) == L"" && FileName[1] != L'\\')
      {
        FileName = IncludeTrailingBackslash(LocalDirView->PathName) + FileName;
      }
      if (ExtractFileDrive(PointTo) == L"" && PointTo[1] != L'\\')
      {
        PointTo = IncludeTrailingBackslash(LocalDirView->PathName) + PointTo;
      }
      if (ExtractFileExt(FileName) == L"")
      {
        FileName = FileName + L".lnk";
      }

      if (Edit)
      {
        DeleteFileChecked(FileName);
      }
      if (!CreateFileShortCut(PointTo, FileName, L""))
      {
        throw Exception(CREATE_SHORTCUT_ERROR);
      }
    }
  }
  else
  {
    TCustomScpExplorerForm::AddEditLink(Side, Add);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::DoOpenDirectoryDialog(TOpenDirectoryMode Mode,
  TOperationSide Side)
{
  bool UseLocationProfiles;
  do
  {
    UseLocationProfiles = WinConfiguration->UseLocationProfiles;
    // Location profiles dialog is not ready to be run without terminal
    if (UseLocationProfiles && (Terminal != NULL))
    {
      TStrings * LocalDirectories = NULL;
      TStrings * RemoteDirectories = NULL;
      try
      {
        LocalDirectories = CreateVisitedDirectories(osLocal);
        RemoteDirectories = CreateVisitedDirectories(osRemote);

        UnicodeString Local = LocalDirView->PathName;
        UnicodeString Remote = RemoteDirView->PathName;

        if (LocationProfilesDialog(Mode, Side, Local, Remote, LocalDirectories,
              RemoteDirectories, Terminal))
        {
          DoOpenBookmark(Local, Remote);
        }
      }
      __finally
      {
        delete LocalDirectories;
        delete RemoteDirectories;
      }
    }
    else
    {
      TSynchronizedBrowsingGuard SynchronizedBrowsingGuard;
      TCustomScpExplorerForm::DoOpenDirectoryDialog(Mode, Side);
    }

    // for second and further rounds, always do browse only
    Mode = odBrowse;
  }
  while (UseLocationProfiles != WinConfiguration->UseLocationProfiles);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::DoOpenBookmark(UnicodeString Local, UnicodeString Remote)
{
  TSynchronizedBrowsingGuard SynchronizedBrowsingGuard;
  // make sure that whatever path is valid it is opened first and only
  // after that an eventual error is reported
  try
  {
    if (!Local.IsEmpty())
    {
      LocalDirView->Path = Local;
    }
  }
  __finally
  {
    if (!Remote.IsEmpty())
    {
      // While we might get here when the session is closed (from location profiles),
      // it's not a problem as the Path setter is noop then
      RemoteDirView->Path = Remote;
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::OpenBookmark(UnicodeString Local, UnicodeString Remote)
{
  bool Result;
  if (WinConfiguration->UseLocationProfiles)
  {
    DoOpenBookmark(Local, Remote);
    Result = true;
  }
  else
  {
    Result = TCustomScpExplorerForm::OpenBookmark(Local, Remote);
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDirViewDDTargetHasDropHandler(
  TObject * /*Sender*/, TListItem * Item, int & /*Effect*/, bool & DropHandler)
{
  // When drop target is not directory, it is probably file type, which have
  // associated drop handler (such as EXE file). In this case we
  // cannot allow drop when when using shellex,
  // as drop handlers are disabled, so drop would error
  // (see TShellExtension.DropHandler assignment in ConfigurationChanged),
  if (WinConfiguration->DDExtEnabled &&
      !LocalDirView->ItemIsDirectory(Item))
  {
    DropHandler = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalFileControlDDDragOver(TObject * /*Sender*/,
  int grfKeyState, TPoint & /*Point*/, int & dwEffect)
{
  if (IsFileControl(DropSourceControl, osRemote))
  {
    if (((grfKeyState & (MK_CONTROL | MK_SHIFT)) == 0) ||
        (((grfKeyState & (MK_CONTROL | MK_SHIFT)) == MK_SHIFT) &&
          !WinConfiguration->DDAllowMoveInit))
    {
      dwEffect = DROPEFFECT_Copy;
    }
  }
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::DDGetTarget(
  UnicodeString & Directory, bool & ForceQueue, bool & Internal)
{
  bool Result;
  if (!FDDExtTarget.IsEmpty())
  {
    Directory = FDDExtTarget;
    FDDExtTarget = L"";
    Result = true;
    Internal = true;
    ForceQueue = false;
  }
  else
  {
    Result = TCustomScpExplorerForm::DDGetTarget(Directory, ForceQueue, Internal);
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::DDExtInitDrag(TFileList * FileList,
  bool & Created)
{
  FDDExtTarget = L"";
  TCustomScpExplorerForm::DDExtInitDrag(FileList, Created);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalFileControlDDFileOperation(
  TObject * /*Sender*/, int dwEffect, UnicodeString SourcePath,
  UnicodeString TargetPath, bool & DoOperation)
{
  if (IsFileControl(DropSourceControl, osRemote))
  {
    UnicodeString TargetDirectory;
    if (InternalDDDownload(TargetDirectory))
    {
      // See TCustomScpExplorerForm::QueueDDProcessDropped
      if (FDDExtMapFile != NULL)
      {
        FDDExtTarget = TargetDirectory;
      }
      else
      {
        DebugAssert(FInternalDDDownloadList->Count > 0);
        DebugAssert(dwEffect == DROPEFFECT_Copy || dwEffect == DROPEFFECT_Move);
        TGUICopyParamType CopyParams = GUIConfiguration->CurrentCopyParam;
        TTransferType TransferType = dwEffect == DROPEFFECT_Copy ? ttCopy : ttMove;
        if (FDDMoveSlipped)
        {
          TransferType = ttMove;
        }
        int Options =
          FLAGMASK(DraggingAllFilesFromDirView(osRemote, FInternalDDDownloadList), coAllFiles);
        if (CopyParamDialog(tdToLocal, TransferType,
              false, FInternalDDDownloadList, TargetDirectory, CopyParams,
              (WinConfiguration->DDTransferConfirmation != asOff), true, Options))
        {
          int Params =
            (TransferType == ttMove ? cpDelete : 0);
          DDDownload(FInternalDDDownloadList, TargetDirectory,
            &CopyParams, Params);
          Configuration->Usage->Inc(L"DownloadsDragDropInternal");
          FInternalDDDownloadList->Clear();
        }
      }
      DoOperation = false;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RemoteFileControlDDFileOperationExecuted(
  TObject * /*Sender*/, int dwEffect, UnicodeString /*SourcePath*/,
  UnicodeString /*TargetPath*/)
{
  if ((dwEffect == DROPEFFECT_Move) &&
      IsFileControl(DropSourceControl, osLocal))
  {
    ReloadLocalDirectory();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDirViewEnter(TObject * /*Sender*/)
{
  SideEnter(osLocal);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDriveViewEnter(TObject * /*Sender*/)
{
  MakeNextInTabOrder(LocalDirView, LocalDriveView);
  SideEnter(osLocal);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SideEnter(TOperationSide Side)
{
  if (Visible && (FCurrentSide != Side))
  {
    // this may get called yet before controls are initialized
    CommandLineCombo->Strings->Clear();
    FCommandLineComboPopulated = false;
  }
  TCustomScpExplorerForm::SideEnter(Side);
  if (Visible)
  {
    UpdateControls();
    UpdatePanelsPathLabelsStatus();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::UpdatePanelsPathLabelsStatus()
{
  LocalPathLabel->UpdateStatus();
  RemotePathLabel->UpdateStatus();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::OpenConsole(UnicodeString Command)
{
  SaveCommandLine();
  try
  {
    TCustomScpExplorerForm::OpenConsole(Command);
  }
  __finally
  {
    FCommandLineComboPopulated = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SaveCommandLine()
{
  if (FCommandLineComboPopulated)
  {
    CustomWinConfiguration->History[
      FCurrentSide == osRemote ? L"Commands" : L"LocalCommands"] =
        CommandLineCombo->Strings;
  }
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::ExecuteCommandLine()
{
  UnicodeString Command = CommandLineCombo->Text;
  bool Result =
    !NonVisualDataModule->Busy &&
    !Command.IsEmpty() &&
      ((FCurrentSide != osRemote) ||
       (Terminal->AllowedAnyCommand(Command) &&
        EnsureCommandSessionFallback(fcAnyCommand)));
  if (Result)
  {
    CommandLinePopulate();
    SaveToHistory(CommandLineCombo->Strings, Command);
    CommandLineCombo->Text = L"";
    if (FCurrentSide == osRemote)
    {
      OpenConsole(Command);
    }
    else
    {
      ExecuteShellChecked(Command);
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::CommandLinePopulate()
{
  if (!FCommandLineComboPopulated)
  {
    TStrings * CommandsHistory;
    CommandsHistory = CustomWinConfiguration->History[
      FCurrentSide == osRemote ? L"Commands" : L"LocalCommands"];
    if ((CommandsHistory != NULL) && (CommandsHistory->Count > 0))
    {
      CommandLineCombo->Strings = CommandsHistory;
    }
    else
    {
      CommandLineCombo->Strings->Clear();
    }
    FCommandLineComboPopulated = true;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::GoToCommandLine()
{
  ComponentVisible[fcCommandLinePanel] = true;
  if (CommandLineCombo->Enabled)
  {
    ClickToolbarItem(CommandLineCombo, true);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::GoToTree()
{
  if (FCurrentSide == osLocal)
  {
    ComponentVisible[fcLocalTree] = true;
    LocalDriveView->SetFocus();
  }
  else
  {
    TCustomScpExplorerForm::GoToTree();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::PanelExportStore(TOperationSide Side,
  TPanelExport Export, TPanelExportDestination Destination,
  TStrings * ExportData)
{
  if (Destination == pedCommandLine)
  {
    ComponentVisible[fcCommandLinePanel] = true;

    UnicodeString Buf;
    for (int Index = 0; Index < ExportData->Count; Index++)
    {
      Buf += ExportData->Strings[Index] + L" ";
    }

    CommandLineCombo->Text = CommandLineCombo->Text + Buf;
  }
  else
  {
    TCustomScpExplorerForm::PanelExportStore(Side, Export, Destination, ExportData);
  }
}
//---------------------------------------------------------------------------
int __fastcall TScpCommanderForm::GetStaticComponentsHeight()
{
  return TCustomScpExplorerForm::GetStaticComponentsHeight() +
    (BottomDock->Visible ? BottomDock->Height : 0) +
    (QueueSeparatorPanel->Visible ? QueueSeparatorPanel->Height : 0) +
    (StatusBar->Visible ? StatusBar->Height : 0);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::SysResizing(unsigned int Cmd)
{
  TCustomScpExplorerForm::SysResizing(Cmd);

  if ((Cmd == SC_MAXIMIZE) ||
      ((Cmd == SC_DEFAULT) && (WindowState != wsMaximized)))
  {
    FNormalPanelsWidth = LocalPanel->Width + RemotePanel->Width;
  }
  else if ((Cmd == SC_RESTORE) ||
    ((Cmd == SC_DEFAULT) && (WindowState == wsMaximized)))
  {
    if (FNormalPanelsWidth >= 0)
    {
      Panel(true)->Width = static_cast<int>(FLeftPanelWidth * FNormalPanelsWidth);
      FNormalPanelsWidth = -1;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::Resize()
{
  TCustomScpExplorerForm::Resize();

  LeftPanelWidth = FLastLeftPanelWidth;
  UpdateControls();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalFileControlDDMenuPopup(TObject * /*Sender*/,
  HMENU AMenu, IDataObject * /*DataObj*/, int /*AMinCustCmd*/, int /*grfKeyState*/,
  TPoint & /*pt*/)
{
  if (IsFileControl(DropSourceControl, osRemote) &&
      !WinConfiguration->DDAllowMoveInit)
  {
    // index of copy item
    int Index = GetMenuDefaultItem(AMenu, TRUE, 0);
    DebugAssert(Index >= 0);

    UnicodeString Caption = Dragdrop_MIMoveStr;

    MENUITEMINFO MI;
    memset(&MI, 0, sizeof(MI));
    MI.cbSize = sizeof(MI);
    MI.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
    MI.fType = MFT_STRING;
    MI.wID = 1 /*DragDrop::CmdMove*/;
    MI.dwTypeData = Caption.c_str();
    MI.cch = Caption.Length();
    MI.fState = MFS_ENABLED;
    InsertMenuItem(AMenu, Index, TRUE, &MI);

    if (FDDMoveSlipped)
    {
      SetMenuDefaultItem(AMenu, Index, TRUE);
    }

    FDDMoveSlipped = false;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::PathLabelDblClick(TObject * Sender)
{
  OpenDirectory(Sender == LocalPathLabel ? osLocal : osRemote);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalPathLabelGetStatus(
  TCustomPathLabel * /*Sender*/, bool & Active)
{
  // WORKAROUND this strange form is here to make borland compiler work :-)
  Active = Active || LocalDriveView->Focused();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RemotePathLabelGetStatus(
  TCustomPathLabel * /*Sender*/, bool & Active)
{
  // WORKAROUND this strange form is here to make borland compiler work :-)
  Active = Active || RemoteDriveView->Focused();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalPathLabelPathClick(
  TCustomPathLabel * /*Sender*/, UnicodeString Path)
{
  if (SamePaths(Path, LocalDirView->Path))
  {
    OpenDirectory(osLocal);
  }
  else
  {
    LocalDirView->Path = Path;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RemotePathLabelPathClick(
  TCustomPathLabel * /*Sender*/, UnicodeString Path)
{
  if (UnixSamePath(Path, RemoteDirView->Path))
  {
    OpenDirectory(osRemote);
  }
  else
  {
    RemoteDirView->Path = Path;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDirViewFileIconForName(
  TObject * /*Sender*/, TListItem * /*Item*/, UnicodeString & FileName)
{
  UnicodeString PartialExt = Configuration->PartialExt;
  if (AnsiSameText(ExtractFileExt(FileName), PartialExt))
  {
    FileName.SetLength(FileName.Length() - PartialExt.Length());
  }
  if (WinConfiguration->LocalIconsByExt)
  {
    FileName = ExtractFileName(FileName);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDirViewUpdateStatusBar(
  TObject * /*Sender*/, const TStatusFileInfo & FileInfo)
{
  UpdateFileStatusBar(LocalStatusBar, FileInfo, osLocal);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RemoteDirViewUpdateStatusBar(
  TObject * /*Sender*/, const TStatusFileInfo & FileInfo)
{
  UpdateFileStatusBar(RemoteStatusBar, FileInfo, osRemote);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalStatusBarClick(TObject * /*Sender*/)
{
  LocalDirView->SetFocus();
}
//---------------------------------------------------------------------------
UnicodeString __fastcall TScpCommanderForm::PathForCaption()
{
  UnicodeString Result;
  if (FCurrentSide == osLocal)
  {
    // for consistency do not show even local path when there is no terminal
    if (Terminal != NULL)
    {
      switch (WinConfiguration->PathInCaption)
      {
        case picShort:
          {
            Result = ExtractFileName(LocalDirView->PathName);
            if (Result.IsEmpty())
            {
              Result = LocalDirView->PathName;
            }
          }
          break;

        case picFull:
          Result = LocalDirView->PathName;
          break;
      }
    }
  }
  else
  {
    Result = TCustomScpExplorerForm::PathForCaption();
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::GetTransferPresetAutoSelectData(
  TCopyParamRuleData & Data)
{
  TCustomScpExplorerForm::GetTransferPresetAutoSelectData(Data);
  Data.LocalDirectory = LocalDirView->PathName;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::BeforeAction()
{
  TCustomScpExplorerForm::BeforeAction();

  if (LocalDirView->ItemFocused != NULL)
  {
    LocalDirView->ItemFocused->CancelEdit();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RemoteDirViewPathChange(TCustomDirView * /*Sender*/)
{
  UpdateRemotePathComboBox(false);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::UpdateImages()
{
  TCustomScpExplorerForm::UpdateImages();

  TImageList * ImageList = ShellImageListForControl(this, ilsSmall);
  RemotePathComboBox->Images = ImageList;
  RemotePathComboBox->SubMenuImages = ImageList;
  LocalPathComboBox->Images = ImageList;
  LocalPathComboBox->SubMenuImages = ImageList;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalPathComboUpdateDrives()
{
  FLocalSpecialPaths = 0;
  TStrings* Strings = LocalPathComboBox->Strings;
  Strings->BeginUpdate();
  try
  {
    Strings->Clear();
    FLocalPathComboBoxPaths->Clear();
    Strings->Add(LoadStr(SPECIAL_FOLDER_MY_DOCUMENTS));
    FLocalPathComboBoxPaths->AddObject(GetPersonalFolder(),
      (TObject *)DriveInfo->SpecialFolder[CSIDL_PERSONAL]->ImageIndex);
    FLocalSpecialPaths++;
    Strings->Add(LoadStr(SPECIAL_FOLDER_DESKTOP));
    FLocalPathComboBoxPaths->AddObject(GetDesktopFolder(),
      (TObject *)DriveInfo->SpecialFolder[CSIDL_DESKTOP]->ImageIndex);
    FLocalSpecialPaths++;

    std::unique_ptr<TStrings> Drives(LocalDriveView->GetDrives());
    for (int Index = 0; Index < Drives->Count; Index++)
    {
      UnicodeString Drive = Drives->Strings[Index];
      if (DriveInfo->Get(Drive)->Valid)
      {
        UnicodeString Caption = DriveInfo->GetPrettyName(Drive);
        if (DriveInfo->IsRealDrive(Drive))
        {
          Caption.Insert(L"&", 0);
        }
        Strings->Add(Caption);
        UnicodeString RootPath = DriveInfo->GetDriveRoot(Drive);
        int ImageIndex = DriveInfo->GetImageIndex(Drive);
        FLocalPathComboBoxPaths->AddObject(RootPath, reinterpret_cast<TObject *>(ImageIndex));
      }
    }
  }
  __finally
  {
    Strings->EndUpdate();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalPathComboUpdate()
{
  // this may get called even after destructor finishes
  // (e.g. from SetDockAllowDrag invoked [indirectly] from StoreParams)
  if (FLocalPathComboBoxPaths != NULL)
  {
    DebugAssert(FLocalPathComboBoxPaths->Count == LocalPathComboBox->Strings->Count);

    int Index = 0;
    while ((Index < FLocalPathComboBoxPaths->Count) &&
           !SamePaths(FLocalPathComboBoxPaths->Strings[Index],
             LocalDirView->Path.SubString(1, FLocalPathComboBoxPaths->Strings[Index].Length())))
    {
      Index++;
    }

    // what to do if not?
    if (Index < FLocalPathComboBoxPaths->Count)
    {
      LocalPathComboBox->ItemIndex = Index;
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDirViewPathChange(TCustomDirView * /*Sender*/)
{
  LocalPathComboUpdate();
  if (IsUncPath(LocalDirView->Path))
  {
    Configuration->Usage->Inc(L"BrowsedUncPath");
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalPathComboBoxCancel(TObject * /*Sender*/)
{
  LocalPathComboUpdate();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalPathComboBoxAdjustImageIndex(
  TTBXComboBoxItem * /*Sender*/, const UnicodeString AText, int AIndex,
  int & ImageIndex)
{
  // this may get called even before constructor starts
  // (e.g. from FixControlsPlacement)
  if (FLocalPathComboBoxPaths != NULL)
  {
    DebugAssert(FLocalPathComboBoxPaths->Count == LocalPathComboBox->Strings->Count);
    DebugAssert(AIndex < FLocalPathComboBoxPaths->Count);

    if (AIndex < 0)
    {
      AIndex = LocalPathComboBox->ItemIndex;
    }

    if (AIndex >= 0)
    {
      ImageIndex = int(FLocalPathComboBoxPaths->Objects[AIndex]);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalPathComboBoxItemClick(TObject * /*Sender*/)
{
  DebugAssert(FLocalPathComboBoxPaths->Count == LocalPathComboBox->Strings->Count);
  DebugAssert((LocalPathComboBox->ItemIndex >= 0) && (LocalPathComboBox->ItemIndex < FLocalPathComboBoxPaths->Count));

  UnicodeString Path = FLocalPathComboBoxPaths->Strings[LocalPathComboBox->ItemIndex];
  if (LocalPathComboBox->ItemIndex >= FLocalSpecialPaths)
  {
    LocalDirView->ExecuteDrive(DriveInfo->GetDriveKey(Path));
  }
  else
  {
    LocalDirView->Path = Path;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::ToolbarItemResize(TTBXCustomDropDownItem * Item, int Width)
{
  TCustomScpExplorerForm::ToolbarItemResize(Item, Width);
  if ((Item == LocalPathComboBox) ||
      (Item == RemotePathComboBox) ||
      (Item == CommandLineCombo))
  {
    dynamic_cast<TTBXComboBoxItem *>(Item)->MinListWidth = Width - 4;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::CommandLineComboPopup(
  TTBCustomItem * /*Sender*/, bool /*FromLink*/)
{
  CommandLinePopulate();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::CommandLineComboBeginEdit(
  TTBEditItem * /*Sender*/, TTBEditItemViewer * /*Viewer*/, TEdit *EditControl)
{
  InstallPathWordBreakProc(EditControl);
  FCommandLineComboEdit = EditControl;
  FToolbarEditOldWndProc = EditControl->WindowProc;
  EditControl->WindowProc = CommandLineComboEditWndProc;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::ExitToolbar()
{
  CurrentPanel()->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::CommandLineComboEditWndProc(TMessage & Message)
{
  bool Handled = false;
  if (Message.Msg == WM_CHAR)
  {
    switch (reinterpret_cast<TWMChar &>(Message).CharCode)
    {
      case VK_ESCAPE:
        CommandLineCombo->Text = L"";
        ExitToolbar();
        Handled = true;
        break;

      case VK_TAB:
        CommandLineCombo->Text = FCommandLineComboEdit->Text;
        ExitToolbar();
        Handled = true;
        break;

      case VK_RETURN:
        CommandLineCombo->Text = FCommandLineComboEdit->Text;
        ExitToolbar();
        if (!ExecuteCommandLine())
        {
          // re-enter command line
          // (most probably exited by now as message dialog was shown)
          GoToCommandLine();
        }
        Handled = true;
        break;
    }
  }

  if (!Handled)
  {
    FToolbarEditOldWndProc(Message);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDriveViewRefreshDrives(TObject * /*Sender*/)
{
  LocalPathComboUpdateDrives();
  LocalPathComboUpdate();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::HomeDirectory(TOperationSide Side)
{
  bool SynchronizeBrowsing = NonVisualDataModule->SynchronizeBrowsingAction->Checked;

  TSynchronizedBrowsingGuard SynchronizedBrowsingGuard;

  TCustomScpExplorerForm::HomeDirectory(Side);

  if (SynchronizeBrowsing)
  {
    TCustomScpExplorerForm::HomeDirectory(GetOtherSize(Side));
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::QueueSubmenuItemPopup(
  TTBCustomItem * /*Sender*/, bool /*FromLink*/)
{
  NonVisualDataModule->QueueSpeedComboBoxItemUpdate(QueueSpeedComboBoxItem);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::DoFocusRemotePath(TTerminal * Terminal, const UnicodeString & Path)
{
  TSynchronizedBrowsingGuard SynchronizedBrowsingGuard;
  TCustomScpExplorerForm::DoFocusRemotePath(Terminal, Path);
}
//---------------------------------------------------------------------------
TOperationSide __fastcall TScpCommanderForm::GetOtherSize(TOperationSide Side)
{
  Side = GetSide(Side);
  return ReverseOperationSide(Side);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::HistoryGo(TOperationSide Side, int Index)
{
  TOperationSide OtherSide = GetOtherSize(Side);
  if (NonVisualDataModule->SynchronizeBrowsingAction->Checked &&
      ((Index < 0) ? (-Index < DirView(OtherSide)->BackCount) : (Index < DirView(OtherSide)->ForwardCount)))
  {
    TSynchronizedBrowsingGuard SynchronizedBrowsingGuard;
    TCustomScpExplorerForm::HistoryGo(Side, Index);
    TCustomScpExplorerForm::HistoryGo(OtherSide, Index);
  }
  else
  {
    TCustomScpExplorerForm::HistoryGo(Side, Index);
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::DirViewHistoryGo(
  TCustomDirView * Sender, int Index, bool & Cancel)
{
  TOperationSide Side = (Sender == DirView(osRemote) ? osRemote : osLocal);
  HistoryGo(Side, Index);
  Cancel = true;
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::EligibleForImageDisplayMode(TTBCustomItem * Item)
{
  return
    TCustomScpExplorerForm::EligibleForImageDisplayMode(Item) &&
    ((Item->Parent == NULL) || (Item->Parent->ParentComponent != Toolbar2Toolbar));
}
//---------------------------------------------------------------------------
bool __fastcall TScpCommanderForm::UpdateToolbarDisplayMode()
{
  bool Result = TCustomScpExplorerForm::UpdateToolbarDisplayMode();
  if (Result)
  {
    // command line combo width needs to be updated as caption visibility has changed
    ToolBarResize(CommandLineToolbar);
  }
  return Result;
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::QueueLabelUpdateStatus()
{
  TCustomScpExplorerForm::QueueLabelUpdateStatus();
  // this is here to deactivate panels path labels when moving focus from
  // directory tree to queue
  UpdatePanelsPathLabelsStatus();
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalDirViewContextPopup(TObject * /*Sender*/,
  TPoint & MousePos, bool & Handled)
{
  if (!WinConfiguration->ScpCommander.SystemContextMenu && !FForceSystemContextMenu)
  {
    DirViewContextPopupDefaultItem(osLocal, NonVisualDataModule->LocalOpenMenuItem, dcaOpen);
    DirViewContextPopupDefaultItem(osLocal, NonVisualDataModule->LocalEditMenuItem, dcaEdit);
    DirViewContextPopupDefaultItem(osLocal, NonVisualDataModule->LocalCopyMenuItem, dcaCopy);

    DirViewContextPopup(osLocal, fcLocalPopup, MousePos);
    Handled = true;
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::DisplaySystemContextMenu()
{
  TAutoFlag Flag(FForceSystemContextMenu);
  if ((FLastContextPopupScreenPoint.x >= 0) && (FLastContextPopupScreenPoint.Y >= 0))
  {
    LocalDirView->DisplayContextMenu(FLastContextPopupScreenPoint);
  }
  else
  {
    LocalDirView->DisplayContextMenuInSitu();
  }
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalStatusBarPanelClick(TTBXCustomStatusBar * /*Sender*/,
  TTBXStatusPanel * Panel)
{
  FileStatusBarPanelClick(Panel, osLocal);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RemoteStatusBarPanelClick(TTBXCustomStatusBar * /*Sender*/,
  TTBXStatusPanel *Panel)
{
  FileStatusBarPanelClick(Panel, osRemote);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::GoToAddress()
{
  OpenDirectory(GetSide(osCurrent));
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::RemotePathLabelMaskClick(TObject * /*Sender*/)
{
  Filter(osRemote);
}
//---------------------------------------------------------------------------
void __fastcall TScpCommanderForm::LocalPathLabelMaskClick(TObject * /*Sender*/)
{
  Filter(osLocal);
}
//---------------------------------------------------------------------------
