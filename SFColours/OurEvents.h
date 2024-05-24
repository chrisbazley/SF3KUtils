/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Client-allocated Toolbox event numbers
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
  EventCode_CreateObjColours   = 0x0a,
  EventCode_CreateHillColours  = 0x0b,
  EventCode_QuickSave          = 0x0c,
  EventCode_AbortDrag          = 0x0d,
  EventCode_Copy               = 0x0e,
  EventCode_Paste              = 0x0f,
  EventCode_NewView            = 0x10,
  EventCode_Undo               = 0x40,
  EventCode_Redo               = 0x41,
  EventCode_WindowsToFront     = 0x43,
};

#endif
