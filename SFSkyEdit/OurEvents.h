/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Client task-specific Toolbox event numbers
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef OurEvents_h
#define OurEvents_h

/* Toolbox event codes */
enum
{
  EventCode_FileInfo           = 0x01,
  EventCode_CloseWindow        = 0x02,
  EventCode_Help               = 0x03,
  EventCode_SaveFile           = 0x04,
  EventCode_Smooth             = 0x05,
  EventCode_SelectAll          = 0x06,
  EventCode_ClearSelection     = 0x07,
  EventCode_SetColour          = 0x08,
  EventCode_Quit               = 0x09,
  EventCode_PreviewSetCompOff  = 0x0a,
  EventCode_PreviewSetStarsAlt = 0x0b,
  EventCode_PreviewUp          = 0x0c,
  EventCode_PreviewDown        = 0x0d,
  EventCode_PreviewClose       = 0x0e,
  EventCode_Preview            = 0x0f,
  EventCode_CaretUp            = 0x10,
  EventCode_CaretDown          = 0x11,
  EventCode_PageUp             = 0x12,
  EventCode_PageDown           = 0x13,
  EventCode_Insert             = 0x14,
  EventCode_Copy               = 0x15,
  EventCode_Cut                = 0x16,
  EventCode_Paste              = 0x17,
  EventCode_Delete             = 0x18,
  EventCode_Interpolate        = 0x19,
  EventCode_NewView            = 0x1a,
  EventCode_CaretToEnd         = 0x20,
  EventCode_CaretToStart       = 0x21,
  EventCode_QuickSave          = 0x23,
  EventCode_AbortDrag          = 0x24,
  EventCode_PreviewRotateRight = 0x25,
  EventCode_PreviewRotateLeft  = 0x26,
  EventCode_PreviewToolbars    = 0x27,
  EventCode_PreviewNewStars    = 0x28,
  EventCode_PreviewTiltUp      = 0x29,
  EventCode_PreviewTiltDown    = 0x30,
  EventCode_PreviewSave        = 0x31,
  EventCode_PreviewScale       = 0x32,
  EventCode_Goto               = 0x33,
  EventCode_PreviewDefault     = 0x34,
  EventCode_Undo               = 0x40,
  EventCode_Redo               = 0x41,
  EventCode_NewFile            = 0x42,
  EventCode_WindowsToFront     = 0x43,
};

#endif
