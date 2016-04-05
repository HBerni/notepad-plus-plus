// This file is part of Notepad++ project
// Copyright (C)2003 Don HO <don.h@free.fr>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// Note that the GPL places important restrictions on "derived works", yet
// it does not provide a detailed definition of that term.  To avoid
// misunderstandings, we consider an application to constitute a
// "derivative work" for the purpose of this license if it does any of the
// following:
// 1. Integrates source code from Notepad++.
// 2. Integrates/includes/aggregates Notepad++ into a proprietary executable
//    installer, such as those produced by InstallShield.
// 3. Links to a library or executes a program that does any of the above.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.



#include <stdexcept>
#include "TabBar.h"
#include "Parameters.h"

#define	IDC_DRAG_TAB     1404
#define	IDC_DRAG_INTERDIT_TAB 1405
#define	IDC_DRAG_PLUS_TAB 1406
#define	IDC_DRAG_OUT_TAB 1407

bool TabBarPlus::_doDragNDrop = false;

bool TabBarPlus::_drawTopBar = true;
bool TabBarPlus::_drawInactiveTab = true;
bool TabBarPlus::_drawTabCloseButton = false;
bool TabBarPlus::_isDbClk2Close = false;
bool TabBarPlus::_isCtrlVertical = false;
bool TabBarPlus::_isCtrlMultiLine = false;

COLORREF TabBarPlus::_activeTextColour = ::GetSysColor(COLOR_BTNTEXT);
COLORREF TabBarPlus::_activeTopBarFocusedColour = RGB(250, 170, 60);
COLORREF TabBarPlus::_activeTopBarUnfocusedColour = RGB(250, 210, 150);
COLORREF TabBarPlus::_inactiveTextColour = grey;
COLORREF TabBarPlus::_inactiveBgColour = RGB(192, 192, 192);

HWND TabBarPlus::_hwndArray[nbCtrlMax] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
int TabBarPlus::_nbCtrl = 0;

bool TabBarPlus::_redrawingHighlights = false;

void TabBar::init(HINSTANCE hInst, HWND parent, bool isVertical, bool isTraditional, bool isMultiLine)
{
	Window::init(hInst, parent);
	int vertical = isVertical?(TCS_VERTICAL | TCS_MULTILINE | TCS_RIGHTJUSTIFY):0;
	_isTraditional = isTraditional;
	_isVertical = isVertical;
	_isMultiLine = isMultiLine;

	INITCOMMONCONTROLSEX icce;
	icce.dwSize = sizeof(icce);
	icce.dwICC = ICC_TAB_CLASSES;
	InitCommonControlsEx(&icce);
    int multiLine = isMultiLine?(_isTraditional?TCS_MULTILINE:0):0;

	int style = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE |\
        TCS_FOCUSNEVER | TCS_TABS | WS_TABSTOP | vertical | multiLine;

	_hSelf = ::CreateWindowEx(
				0,
				WC_TABCONTROL,
				TEXT("Tab"),
				style,
				0, 0, 0, 0,
				_hParent,
				NULL,
				_hInst,
				0);

	if (!_hSelf)
	{
		throw std::runtime_error("TabBar::init : CreateWindowEx() function return null");
	}
}


void TabBar::destroy()
{
	if (_hFont)
		DeleteObject(_hFont);

	if (_hLargeFont)
		DeleteObject(_hLargeFont);

	if (_hVerticalFont)
		DeleteObject(_hVerticalFont);

	if (_hVerticalLargeFont)
		DeleteObject(_hVerticalLargeFont);

	::DestroyWindow(_hSelf);
	_hSelf = NULL;
}


int TabBar::insertAtEnd(const TCHAR *subTabName)
{
	TCITEM tie;
	tie.mask = TCIF_TEXT | TCIF_IMAGE;
	int index = -1;

	if (_hasImgLst)
		index = 0;
	tie.iImage = index;
	tie.pszText = (TCHAR *)subTabName;
	return int(::SendMessage(_hSelf, TCM_INSERTITEM, _nbItem++, reinterpret_cast<LPARAM>(&tie)));
}


void TabBar::getCurrentTitle(TCHAR *title, int titleLen)
{
	TCITEM tci;
	tci.mask = TCIF_TEXT;
	tci.pszText = title;
	tci.cchTextMax = titleLen-1;
	::SendMessage(_hSelf, TCM_GETITEM, getCurrentTabIndex(), reinterpret_cast<LPARAM>(&tci));
}


void TabBar::setFont(TCHAR *fontName, size_t fontSize)
{
	if (_hFont)
		::DeleteObject(_hFont);

	_hFont = ::CreateFont( fontSize, 0,
						  (_isVertical) ? 900:0,
						  (_isVertical) ? 900:0,
		                   FW_NORMAL,
			               0, 0, 0, 0,
			               0, 0, 0, 0,
				           fontName);
	if (_hFont)
		::SendMessage(_hSelf, WM_SETFONT, reinterpret_cast<WPARAM>(_hFont), 0);
}


void TabBar::activateAt(int index) const
{
	if (getCurrentTabIndex() != index)
		::SendMessage(_hSelf, TCM_SETCURSEL, index, 0);

	TBHDR nmhdr;
	nmhdr.hdr.hwndFrom = _hSelf;
	nmhdr.hdr.code = TCN_SELCHANGE;
	nmhdr.hdr.idFrom = reinterpret_cast<UINT_PTR>(this);
	nmhdr.tabOrigin = index;
}


void TabBar::deletItemAt(size_t index)
{
	if ((index == _nbItem-1))
	{
		//prevent invisible tabs. If last visible tab is removed, other tabs are put in view but not redrawn
		//Therefore, scroll one tab to the left if only one tab visible
		if (_nbItem > 1)
		{
			RECT itemRect;
			::SendMessage(_hSelf, TCM_GETITEMRECT, (WPARAM)index, (LPARAM)&itemRect);
			if (itemRect.left < 5) //if last visible tab, scroll left once (no more than 5px away should be safe, usually 2px depending on the drawing)
			{
				//To scroll the tab control to the left, use the WM_HSCROLL notification
				//Doesn't really seem to be documented anywhere, but the values do match the message parameters
				//The up/down control really is just some sort of scrollbar
				//There seems to be no negative effect on any internal state of the tab control or the up/down control
				int wParam = MAKEWPARAM(SB_THUMBPOSITION, index - 1);
				::SendMessage(_hSelf, WM_HSCROLL, wParam, 0);

				wParam = MAKEWPARAM(SB_ENDSCROLL, index - 1);
				::SendMessage(_hSelf, WM_HSCROLL, wParam, 0);
			}
		}
	}
	::SendMessage(_hSelf, TCM_DELETEITEM, index, 0);
	_nbItem--;
}


void TabBar::setImageList(HIMAGELIST himl)
{
	_hasImgLst = true;
	::SendMessage(_hSelf, TCM_SETIMAGELIST, 0, (LPARAM)himl);
}


void TabBar::reSizeTo(RECT & rc2Ajust)
{
	RECT RowRect;
	int RowCount, TabsLength;

	// Important to do that!
	// Otherwise, the window(s) it contains will take all the resouce of CPU
	// We don't need to resize the contained windows if they are even invisible anyway
	display(rc2Ajust.right > 10);
	RECT rc = rc2Ajust;
	Window::reSizeTo(rc);

	// Do our own calculations because TabCtrl_AdjustRect doesn't work
	// on vertical or multi-lined tab controls

	RowCount = TabCtrl_GetRowCount(_hSelf);
	TabCtrl_GetItemRect(_hSelf, 0, &RowRect);
	if (_isTraditional)
	{
		TabCtrl_AdjustRect(_hSelf, FALSE, &rc2Ajust);
	}
	else if (_isVertical)
	{
		TabsLength  = RowCount * (RowRect.right - RowRect.left);
		TabsLength += GetSystemMetrics(SM_CXEDGE);

		rc2Ajust.left	+= TabsLength;
		rc2Ajust.right	-= TabsLength;
	}
	else
	{
		TabsLength  = RowCount * (RowRect.bottom - RowRect.top);
		TabsLength += GetSystemMetrics(SM_CYEDGE);

		rc2Ajust.top	+= TabsLength;
		rc2Ajust.bottom -= TabsLength;
	}
}


void TabBarPlus::destroy()
{
	KillTimer(_hSelf, 1);

	TabBar::destroy();
	::DestroyWindow(_tooltips);
	_tooltips = NULL;
}


void TabBarPlus::setHighlight(int index, bool val)
{
	::SendMessage(_hSelf, TCM_HIGHLIGHTITEM, index, MAKELPARAM(val ? TRUE : FALSE, 0));
}


bool TabBarPlus::isHighlighed(int index)
{
	TCITEM tci;
	tci.mask = TCIF_STATE;
	tci.cchTextMax = 0;
	tci.dwStateMask = TCIS_HIGHLIGHTED;

	if (!::SendMessage(_hSelf, TCM_GETITEM, index, reinterpret_cast<LPARAM>(&tci)))
	{
		::MessageBox(NULL, TEXT("! TCM_GETITEM"), TEXT(""), MB_OK);
	}
	return (tci.dwState & TCIS_HIGHLIGHTED) != 0;
}

void TabBarPlus::init(HINSTANCE hInst, HWND parent, bool isVertical, bool isTraditional, bool isMultiLine)
{
	Window::init(hInst, parent);
	int vertical = isVertical?(TCS_VERTICAL | TCS_MULTILINE | TCS_RIGHTJUSTIFY):0;
	_isTraditional = isTraditional;
	_isVertical = isVertical;
	_isMultiLine = isMultiLine;

	INITCOMMONCONTROLSEX icce;
	icce.dwSize = sizeof(icce);
	icce.dwICC = ICC_TAB_CLASSES;
	InitCommonControlsEx(&icce);
    int multiLine = isMultiLine?(_isTraditional?TCS_MULTILINE:0):0;

	int style = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE |\
        TCS_FOCUSNEVER | TCS_TABS | vertical | multiLine;

	style |= TCS_OWNERDRAWFIXED;

	_hSelf = ::CreateWindowEx(
				0,
				WC_TABCONTROL,
				TEXT("Tab"),
				style,
				0, 0, 0, 0,
				_hParent,
				NULL,
				_hInst,
				0);

	if (!_hSelf)
	{
		throw std::runtime_error("TabBarPlus::init : CreateWindowEx() function return null");
	}

	SetTimer(_hSelf, 1, 500, NULL);

	_tooltips = ::CreateWindowEx(
		0,
		TOOLTIPS_CLASS,
		NULL,
		TTS_ALWAYSTIP | TTS_NOPREFIX,
		0, 0, 0, 0,
		_hParent,
		NULL,
		_hInst,
		0);
	if (!_tooltips)
	{
		throw std::runtime_error("TabBarPlus::init : tooltip CreateWindowEx() function return null");
	}
	::SendMessage(_hSelf, TCM_SETTOOLTIPS, (WPARAM)_tooltips, 0);

	if (!_isTraditional)
    {
		if (!_hwndArray[_nbCtrl])
		{
			_hwndArray[_nbCtrl] = _hSelf;
			_ctrlID = _nbCtrl;
		}
		else
		{
			int i = 0;
			bool found = false;
			for ( ; i < nbCtrlMax && !found ; ++i)
				if (!_hwndArray[i])
					found = true;
			if (!found)
			{
				_ctrlID = -1;
				destroy();
				throw std::runtime_error("TabBarPlus::init : Tab Control error - Tab Control # is over its limit");
			}
			_hwndArray[i] = _hSelf;
			_ctrlID = i;
		}
		++_nbCtrl;

        ::SetWindowLongPtr(_hSelf, GWLP_USERDATA, (LONG_PTR)this);
	    _tabBarDefaultProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtr(_hSelf, GWLP_WNDPROC, (LONG_PTR)TabBarPlus_Proc));
    }

	LOGFONT LogFont;

	_hFont = (HFONT)::SendMessage(_hSelf, WM_GETFONT, 0, 0);

	if (_hFont == NULL)
		_hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);

	if (_hLargeFont == NULL)
		_hLargeFont = (HFONT)::GetStockObject(SYSTEM_FONT);

	if (::GetObject(_hFont, sizeof(LOGFONT), &LogFont) != 0)
	{
		LogFont.lfEscapement  = 900;
		LogFont.lfOrientation = 900;
		_hVerticalFont = CreateFontIndirect(&LogFont);

		LogFont.lfWeight = 900;
		_hVerticalLargeFont = CreateFontIndirect(&LogFont);
	}
}


void TabBarPlus::doOwnerDrawTab()
{
	::SendMessage(_hwndArray[0], TCM_SETPADDING, 0, MAKELPARAM(6, 0));
	for (int i = 0 ; i < _nbCtrl ; ++i)
	{
		if (_hwndArray[i])
		{
			LONG_PTR style = ::GetWindowLongPtr(_hwndArray[i], GWL_STYLE);
			if (isOwnerDrawTab())
				style |= TCS_OWNERDRAWFIXED;
			else
				style &= ~TCS_OWNERDRAWFIXED;

			::SetWindowLongPtr(_hwndArray[i], GWL_STYLE, style);
			::InvalidateRect(_hwndArray[i], NULL, TRUE);

			const int paddingSizeDynamicW = NppParameters::getInstance()->_dpiManager.scaleX(6);
			const int paddingSizePlusClosebuttonDynamicW = NppParameters::getInstance()->_dpiManager.scaleX(9);
			::SendMessage(_hwndArray[i], TCM_SETPADDING, 0, MAKELPARAM(_drawTabCloseButton ? paddingSizePlusClosebuttonDynamicW : paddingSizeDynamicW, 0));
		}
	}
}


void TabBarPlus::setColour(COLORREF colour2Set, tabColourIndex i)
{
	switch (i)
	{
		case activeText:
			_activeTextColour = colour2Set;
			break;
		case activeFocusedTop:
			_activeTopBarFocusedColour = colour2Set;
			break;
		case activeUnfocusedTop:
			_activeTopBarUnfocusedColour = colour2Set;
			break;
		case inactiveText:
			_inactiveTextColour = colour2Set;
			break;
		case inactiveBg :
			_inactiveBgColour = colour2Set;
			break;
		default :
			return;
	}
	doOwnerDrawTab();
}


void TabBarPlus::doVertical()
{
	for (int i = 0 ; i < _nbCtrl ; ++i)
	{
		if (_hwndArray[i])
			SendMessage(_hwndArray[i], WM_TABSETSTYLE, isVertical(), TCS_VERTICAL);
	}
}


void TabBarPlus::doMultiLine()
{
	for (int i = 0 ; i < _nbCtrl ; ++i)
	{
		if (_hwndArray[i])
			SendMessage(_hwndArray[i], WM_TABSETSTYLE, isMultiLine(), TCS_MULTILINE);
	}
}


LRESULT TabBarPlus::runProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		// Custom window message to change tab control style on the fly
		case WM_TABSETSTYLE:
		{
			LONG_PTR style = ::GetWindowLongPtr(hwnd, GWL_STYLE);

			if (wParam > 0)
				style |= lParam;
			else
				style &= ~lParam;

			_isVertical  = ((style & TCS_VERTICAL) != 0);
			_isMultiLine = ((style & TCS_MULTILINE) != 0);

			::SetWindowLongPtr(hwnd, GWL_STYLE, style);
			::InvalidateRect(hwnd, NULL, TRUE);

			return TRUE;
		}

		case WM_LBUTTONDOWN :
		{
			if (_drawTabCloseButton)
			{
				int xPos = LOWORD(lParam);
				int yPos = HIWORD(lParam);

				if (_closeButtonZone.isHit(xPos, yPos, _currentHoverTabRect, _isVertical))
				{
					_whichCloseClickDown = getTabIndexAt(xPos, yPos);
					::SendMessage(_hParent, WM_COMMAND, IDM_VIEW_REFRESHTABAR, 0);
					return TRUE;
				}
			}

            ::CallWindowProc(_tabBarDefaultProc, hwnd, Message, wParam, lParam);
			int currentTabOn = ::SendMessage(_hSelf, TCM_GETCURSEL, 0, 0);

			if (wParam == 2)
				return TRUE;

            if (_doDragNDrop)
            {
                _nSrcTab = _nTabDragged = currentTabOn;

                POINT point;
			    point.x = LOWORD(lParam);
			    point.y = HIWORD(lParam);
				::ClientToScreen(hwnd, &point);
			    if(::DragDetect(hwnd, point))
			    {
				    // Yes, we're beginning to drag, so capture the mouse...
				    _isDragging = true;
				    ::SetCapture(hwnd);
			    }
            }

			TBHDR nmhdr;
			nmhdr.hdr.hwndFrom = _hSelf;
			nmhdr.hdr.code = NM_CLICK;
			nmhdr.hdr.idFrom = reinterpret_cast<UINT_PTR>(this);
			nmhdr.tabOrigin = currentTabOn;

			::SendMessage(_hParent, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&nmhdr));

            return TRUE;
		}

		case WM_RBUTTONDOWN :	//rightclick selects tab aswell
		{
			::CallWindowProc(_tabBarDefaultProc, hwnd, WM_LBUTTONDOWN, wParam, lParam);
			return TRUE;
		}

		//#define NPPM_INTERNAL_ISDRAGGING 40926
		case WM_MOUSEMOVE :
		{
			if (_isDragging)
			{
				POINT p;
 				p.x = LOWORD(lParam);
				p.y = HIWORD(lParam);
                exchangeItemData(p);

				// Get cursor position of "Screen"
				// For using the function "WindowFromPoint" afterward!!!
				::GetCursorPos(&_draggingPoint);
				draggingCursor(_draggingPoint);
			    return TRUE;
			}

			if (_drawTabCloseButton)
			{
				int xPos = LOWORD(lParam);
				int yPos = HIWORD(lParam);

				int _currentHoverTabItemOld = _currentHoverTabItem;
				RECT _currentHoverTabRectOld = _currentHoverTabRect;
				bool _isCloseHoverOld = _isCloseHover;

				_currentHoverTabItem = getTabIndexAt(xPos, yPos);
				if (_currentHoverTabItem != -1)
				{
					::SendMessage(_hSelf, TCM_GETITEMRECT, _currentHoverTabItem, (LPARAM)&_currentHoverTabRect);
					_isCloseHover = _closeButtonZone.isHit(xPos, yPos, _currentHoverTabRect, _isVertical);
				}
				else
				{
					SetRectEmpty(&_currentHoverTabRect);
					_isCloseHover = false;
				}

				if (_currentHoverTabItem != _currentHoverTabItemOld || _isCloseHover != _isCloseHoverOld)
				{
					if (_isCloseHoverOld && (_currentHoverTabItem != _currentHoverTabItemOld || !_isCloseHover))
						InvalidateRect(hwnd, &_currentHoverTabRectOld, FALSE);
					if (_isCloseHover)
						InvalidateRect(hwnd, &_currentHoverTabRect, FALSE);
				}

				if (_isCloseHover)
				{
					TRACKMOUSEEVENT tme = {};
					tme.cbSize = sizeof(tme);
					tme.dwFlags = TME_LEAVE;
					tme.hwndTrack = hwnd;
					TrackMouseEvent(&tme);
				}
			}
			break;
		}

		case WM_MOUSELEAVE:
		{
			if (_isCloseHover)
				InvalidateRect(hwnd, &_currentHoverTabRect, FALSE);
			_currentHoverTabItem = -1;
			SetRectEmpty(&_currentHoverTabRect);
			_isCloseHover = false;
			break;
		}

		case WM_LBUTTONUP :
		{
			int xPos = LOWORD(lParam);
			int yPos = HIWORD(lParam);
			int currentTabOn = getTabIndexAt(xPos, yPos);
            if (_isDragging)
			{
				if(::GetCapture() == _hSelf)
					::ReleaseCapture();

				// Send a notification message to the parent with wParam = 0, lParam = 0
				// nmhdr.idFrom = this
				// destIndex = this->_nSrcTab
				// scrIndex  = this->_nTabDragged
				TBHDR nmhdr;
				nmhdr.hdr.hwndFrom = _hSelf;
				nmhdr.hdr.code = _isDraggingInside?TCN_TABDROPPED:TCN_TABDROPPEDOUTSIDE;
				nmhdr.hdr.idFrom = reinterpret_cast<UINT_PTR>(this);
				nmhdr.tabOrigin = currentTabOn;

				::SendMessage(_hParent, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&nmhdr));
				return TRUE;
			}

			if (_drawTabCloseButton)
			{
				if ((_whichCloseClickDown == currentTabOn) && _closeButtonZone.isHit(xPos, yPos, _currentHoverTabRect, _isVertical))
				{
					TBHDR nmhdr;
					nmhdr.hdr.hwndFrom = _hSelf;
					nmhdr.hdr.code = TCN_TABDELETE;
					nmhdr.hdr.idFrom = reinterpret_cast<UINT_PTR>(this);
					nmhdr.tabOrigin = currentTabOn;

					::SendMessage(_hParent, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&nmhdr));

					_whichCloseClickDown = -1;
					return TRUE;
				}
				_whichCloseClickDown = -1;
			}

			break;
		}

		case WM_CAPTURECHANGED :
		{
			if (_isDragging)
			{
				_isDragging = false;
				return TRUE;
			}
			break;
		}

		case WM_DRAWITEM :
		{
			drawItem((DRAWITEMSTRUCT *)lParam);
			return TRUE;
		}

		case WM_KEYDOWN :
		{
			if (wParam == VK_LCONTROL)
				::SetCursor(::LoadCursor(_hInst, MAKEINTRESOURCE(IDC_DRAG_PLUS_TAB)));
			return TRUE;
		}

		case WM_MBUTTONUP:
		{
			int xPos = LOWORD(lParam);
			int yPos = HIWORD(lParam);
			int currentTabOn = getTabIndexAt(xPos, yPos);
			TBHDR nmhdr;
			nmhdr.hdr.hwndFrom = _hSelf;
			nmhdr.hdr.code = TCN_TABDELETE;
			nmhdr.hdr.idFrom = reinterpret_cast<UINT_PTR>(this);
			nmhdr.tabOrigin = currentTabOn;

			::SendMessage(_hParent, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&nmhdr));
			return TRUE;
		}

		case WM_LBUTTONDBLCLK:
		{
			if (_isDbClk2Close)
			{
				int xPos = LOWORD(lParam);
				int yPos = HIWORD(lParam);
				int currentTabOn = getTabIndexAt(xPos, yPos);
				TBHDR nmhdr;
				nmhdr.hdr.hwndFrom = _hSelf;
				nmhdr.hdr.code = TCN_TABDELETE;
				nmhdr.hdr.idFrom = reinterpret_cast<UINT_PTR>(this);
				nmhdr.tabOrigin = currentTabOn;

				::SendMessage(_hParent, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&nmhdr));
			}
			return TRUE;
		}
		case WM_TIMER:
		{
			_redrawingHighlights = true;
			RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
			_redrawingHighlights = false;
			return TRUE;
		}
	}

	return ::CallWindowProc(_tabBarDefaultProc, hwnd, Message, wParam, lParam);
}


void TabBarPlus::drawItem(DRAWITEMSTRUCT *pDrawItemStruct)
{
	RECT rect = pDrawItemStruct->rcItem;

	int nTab = pDrawItemStruct->itemID;
	if (nTab < 0)
	{
		::MessageBox(NULL, TEXT("nTab < 0"), TEXT(""), MB_OK);
		//return ::CallWindowProc(_tabBarDefaultProc, hwnd, Message, wParam, lParam);
	}
	bool isSelected = (nTab == ::SendMessage(_hSelf, TCM_GETCURSEL, 0, 0));

	TCHAR label[MAX_PATH];
	TCITEM tci;
	tci.mask = TCIF_TEXT|TCIF_IMAGE|TCIF_STATE;
	tci.pszText = label;
	tci.cchTextMax = MAX_PATH-1;
	tci.dwStateMask = TCIS_HIGHLIGHTED;

	if (!::SendMessage(_hSelf, TCM_GETITEM, nTab, reinterpret_cast<LPARAM>(&tci)))
	{
		::MessageBox(NULL, TEXT("! TCM_GETITEM"), TEXT(""), MB_OK);
	}
	HDC hDC = pDrawItemStruct->hDC;
	
	DWORD isHighlighted = tci.dwState & TCIS_HIGHLIGHTED;

	if (_redrawingHighlights && isHighlighted == 0)
		return;

	int nSavedDC = ::SaveDC(hDC);

	::SetBkMode(hDC, TRANSPARENT);
	HBRUSH hBrush = ::CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
	::FillRect(hDC, &rect, hBrush);
	::DeleteObject((HGDIOBJ)hBrush);
	
	// equalize drawing areas of active and inactive tabs
	int paddingDynamicTwoX = NppParameters::getInstance()->_dpiManager.scaleX(2);
	int paddingDynamicTwoY = NppParameters::getInstance()->_dpiManager.scaleY(2);
	if (isSelected)
	{
		// the drawing area of the active tab extends on all borders by default
		rect.top += ::GetSystemMetrics(SM_CYEDGE);
		rect.bottom -= ::GetSystemMetrics(SM_CYEDGE);
		rect.left += ::GetSystemMetrics(SM_CXEDGE);
		rect.right -= ::GetSystemMetrics(SM_CXEDGE);
		// the active tab is also slightly higher by default (use this to shift the tab cotent up bx two pixels if tobBar is not drawn)
		if (_isVertical)
		{
			rect.left += _drawTopBar ? paddingDynamicTwoX : 0;
			rect.right -= _drawTopBar ? 0 : paddingDynamicTwoX;
		}
		else
		{
			rect.top += _drawTopBar ? paddingDynamicTwoY : 0;
			rect.bottom -= _drawTopBar ? 0 : paddingDynamicTwoY;
		}
	}
	else
	{
		if (_isVertical)
		{
			rect.left += paddingDynamicTwoX;
			rect.right += paddingDynamicTwoX;
			rect.top -= paddingDynamicTwoY;
			rect.bottom += paddingDynamicTwoY;
		}
		else
		{
			rect.left -= paddingDynamicTwoX;
			rect.right += paddingDynamicTwoX;
			rect.top += paddingDynamicTwoY;
			rect.bottom += paddingDynamicTwoY;
		}
	}

	// draw highlights on tabs (top bar for active tab / darkened background for inactive tab)
	RECT barRect = rect;
	if (isSelected)
	{
		if (_drawTopBar)
		{
			int topBarHeight = NppParameters::getInstance()->_dpiManager.scaleX(4);
			if (_isVertical)
			{
				barRect.left -= NppParameters::getInstance()->_dpiManager.scaleX(2);
				barRect.right = barRect.left + topBarHeight;
			}
			else
			{
				barRect.top -= NppParameters::getInstance()->_dpiManager.scaleY(2);
				barRect.bottom = barRect.top + topBarHeight;
			}

			if (::SendMessage(_hParent, NPPM_INTERNAL_ISFOCUSEDTAB, 0, (LPARAM)_hSelf))
				hBrush = ::CreateSolidBrush(_activeTopBarFocusedColour); // #FAAA3C
			else
				hBrush = ::CreateSolidBrush(_activeTopBarUnfocusedColour); // #FAD296

			::FillRect(hDC, &barRect, hBrush);
			::DeleteObject((HGDIOBJ)hBrush);
		}
	}
	else
	{
		if (_drawInactiveTab)
		{
			SYSTEMTIME time;
			GetSystemTime(&time);

			COLORREF color = isHighlighted ? (time.wSecond%2==0 ? _inactiveBgColour : 0xffff) : _inactiveBgColour;
			hBrush = ::CreateSolidBrush(color);
			::FillRect(hDC, &barRect, hBrush);
			::DeleteObject((HGDIOBJ)hBrush);
		}
	}

	// draw close button
	if (_drawTabCloseButton)
	{
		// 3 status for each inactive tab and selected tab close item :
		// normal / hover / pushed
		int idCloseImg;

		if (_isCloseHover && (_currentHoverTabItem == nTab) && (_whichCloseClickDown == -1)) // hover
			idCloseImg = IDR_CLOSETAB_HOVER;
		else if (_isCloseHover && (_currentHoverTabItem == nTab) && (_whichCloseClickDown == _currentHoverTabItem)) // pushed
			idCloseImg = IDR_CLOSETAB_PUSH;
		else
			idCloseImg = isSelected?IDR_CLOSETAB:IDR_CLOSETAB_INACT;

		HDC hdcMemory;
		hdcMemory = ::CreateCompatibleDC(hDC);
		HBITMAP hBmp = ::LoadBitmap(_hInst, MAKEINTRESOURCE(idCloseImg));
		BITMAP bmp;
		::GetObject(hBmp, sizeof(bmp), &bmp);

		int bmDpiDynamicalWidth = NppParameters::getInstance()->_dpiManager.scaleX(bmp.bmWidth);
		int bmDpiDynamicalHeight = NppParameters::getInstance()->_dpiManager.scaleY(bmp.bmHeight);

		RECT buttonRect = _closeButtonZone.getButtonRectFrom(rect, _isVertical);

		::SelectObject(hdcMemory, hBmp);
		::StretchBlt(hDC, buttonRect.left, buttonRect.top, bmDpiDynamicalWidth, bmDpiDynamicalHeight, hdcMemory, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);
		::DeleteDC(hdcMemory);
		::DeleteObject(hBmp);
	}

	// draw image
	HIMAGELIST hImgLst = (HIMAGELIST)::SendMessage(_hSelf, TCM_GETIMAGELIST, 0, 0);

	if (hImgLst && tci.iImage >= 0)
	{
		IMAGEINFO info;
		ImageList_GetImageInfo(hImgLst, tci.iImage, &info);

		RECT& imageRect = info.rcImage;

		int fromBorder;
		int xPos, yPos;
		if (_isVertical)
		{
			fromBorder = (rect.right - rect.left - (imageRect.right - imageRect.left) + 1) / 2;
			xPos = rect.left + fromBorder;
			yPos = rect.bottom - fromBorder - (imageRect.bottom - imageRect.top);
			rect.bottom -= fromBorder + (imageRect.bottom - imageRect.top);
		}
		else
		{
			fromBorder = (rect.bottom - rect.top - (imageRect.bottom - imageRect.top) + 1) / 2;
			yPos = rect.top + fromBorder;
			xPos = rect.left + fromBorder;
			rect.left += fromBorder + (imageRect.right - imageRect.left);
		}
		ImageList_Draw(hImgLst, tci.iImage, hDC, xPos, yPos, isSelected ? ILD_TRANSPARENT : ILD_SELECTED);
	}

	// draw text
	bool isStandardSize = (::SendMessage(_hParent, NPPM_INTERNAL_ISTABBARREDUCED, 0, 0) == TRUE);

	if (isStandardSize)
	{
		if (_isVertical)
			SelectObject(hDC, _hVerticalFont);
		else
			SelectObject(hDC, _hFont);
	}
	else
	{
		if (_isVertical)
			SelectObject(hDC, _hVerticalLargeFont);
		else
			SelectObject(hDC, _hLargeFont);
	}
	SIZE charPixel;
	::GetTextExtentPoint(hDC, TEXT(" "), 1, &charPixel);
	int spaceUnit = charPixel.cx;

	TEXTMETRIC textMetrics;
	GetTextMetrics(hDC, &textMetrics);
	int textHeight = textMetrics.tmHeight;
	int textDescent = textMetrics.tmDescent;

	int Flags = DT_SINGLELINE | DT_NOPREFIX;

	// This code will read in one character at a time and remove every first ampersand (&).
	// ex. If input "test && test &&& test &&&&" then output will be "test & test && test &&&".
	// Tab's caption must be encoded like this because otherwise tab control would make tab too small or too big for the text.
	TCHAR decodedLabel[MAX_PATH];
	const TCHAR* in = label;
	TCHAR* out = decodedLabel;
	while (*in != 0)
		if (*in == '&')
			while (*(++in) == '&')
				*out++ = *in;
		else
			*out++ = *in++;
	*out = '\0';

	if (_isVertical)
	{
		// center text horizontally (rotated text is positioned as if it were unrotated, therefore manual positioning is necessary)
		Flags |= DT_LEFT;
		Flags |= DT_BOTTOM;
		rect.left += (rect.right - rect.left - textHeight) / 2;
		rect.bottom += textHeight;

		// ignoring the descent when centering (text elements below the base line) is more pleasing to the eye
		rect.left += textDescent / 2;
		rect.right += textDescent / 2;

		// 1 space distance to save icon
		rect.bottom -= spaceUnit;
	}
	else
	{
		// center text vertically
		Flags |= DT_LEFT;
		Flags |= DT_VCENTER;

		// ignoring the descent when centering (text elements below the base line) is more pleasing to the eye
		rect.top += textDescent / 2;
		rect.bottom += textDescent / 2;

		// 1 space distance to save icon
		rect.left += spaceUnit;
	}

	::SetTextColor(hDC, isSelected ? _activeTextColour : _inactiveTextColour);

	::DrawText(hDC, decodedLabel, lstrlen(decodedLabel), &rect, Flags);
	::RestoreDC(hDC, nSavedDC);
}


void TabBarPlus::draggingCursor(POINT screenPoint)
{
	HWND hWin = ::WindowFromPoint(screenPoint);
	if (_hSelf == hWin)
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
	else
	{
		TCHAR className[256];
		::GetClassName(hWin, className, 256);
		if ((!lstrcmp(className, TEXT("Scintilla"))) || (!lstrcmp(className, WC_TABCONTROL)))
		{
			if (::GetKeyState(VK_LCONTROL) & 0x80000000)
				::SetCursor(::LoadCursor(_hInst, MAKEINTRESOURCE(IDC_DRAG_PLUS_TAB)));
			else
				::SetCursor(::LoadCursor(_hInst, MAKEINTRESOURCE(IDC_DRAG_TAB)));
		}
		else if (isPointInParentZone(screenPoint))
			::SetCursor(::LoadCursor(_hInst, MAKEINTRESOURCE(IDC_DRAG_INTERDIT_TAB)));
        else // drag out of application
            ::SetCursor(::LoadCursor(_hInst, MAKEINTRESOURCE(IDC_DRAG_OUT_TAB)));
	}
}

void TabBarPlus::exchangeItemData(POINT point)
{
	// Find the destination tab...
	int nTab = getTabIndexAt(point);

	// The position is over a tab.
	//if (hitinfo.flags != TCHT_NOWHERE)
	if (nTab != -1)
	{
		_isDraggingInside = true;

		if (nTab != _nTabDragged)
		{
			//1. set to focus
			::SendMessage(_hSelf, TCM_SETCURSEL, nTab, 0);

			//2. shift their data, and insert the source
			TCITEM itemData_nDraggedTab, itemData_shift;
			itemData_nDraggedTab.mask = itemData_shift.mask = TCIF_IMAGE | TCIF_TEXT | TCIF_PARAM;
			const int stringSize = 256;
			TCHAR str1[stringSize];
			TCHAR str2[stringSize];

			itemData_nDraggedTab.pszText = str1;
			itemData_nDraggedTab.cchTextMax = (stringSize);

			itemData_shift.pszText = str2;
			itemData_shift.cchTextMax = (stringSize);

			::SendMessage(_hSelf, TCM_GETITEM, _nTabDragged, reinterpret_cast<LPARAM>(&itemData_nDraggedTab));

			if (_nTabDragged > nTab)
			{
				for (int i = _nTabDragged ; i > nTab ; i--)
				{
					::SendMessage(_hSelf, TCM_GETITEM, i-1, reinterpret_cast<LPARAM>(&itemData_shift));
					::SendMessage(_hSelf, TCM_SETITEM, i, reinterpret_cast<LPARAM>(&itemData_shift));
				}
			}
			else
			{
				for (int i = _nTabDragged ; i < nTab ; ++i)
				{
					::SendMessage(_hSelf, TCM_GETITEM, i+1, reinterpret_cast<LPARAM>(&itemData_shift));
					::SendMessage(_hSelf, TCM_SETITEM, i, reinterpret_cast<LPARAM>(&itemData_shift));
				}
			}
			::SendMessage(_hSelf, TCM_SETITEM, nTab, reinterpret_cast<LPARAM>(&itemData_nDraggedTab));

			//3. update the current index
			_nTabDragged = nTab;

			// Tell Notepad_plus to notifiy plugins that a D&D operation was done (so doc index has been changed)
			::SendMessage(_hParent, NPPM_INTERNAL_DOCORDERCHANGED, 0, _nTabDragged);
		}
	}
	else
	{
		//::SetCursor(::LoadCursor(_hInst, MAKEINTRESOURCE(IDC_DRAG_TAB)));
		_isDraggingInside = false;
	}

}


CloseButtonZone::CloseButtonZone()
{
	// TODO: get width/height of close button dynamically
	_width = NppParameters::getInstance()->_dpiManager.scaleX(11);
	_height = NppParameters::getInstance()->_dpiManager.scaleY(11);
}

bool CloseButtonZone::isHit(int x, int y, const RECT & tabRect, bool isVertical) const
{
	RECT buttonRect = getButtonRectFrom(tabRect, isVertical);

	if (x >= buttonRect.left && x <= buttonRect.right && y >= buttonRect.top && y <= buttonRect.bottom)
		return true;

	return false;
}

RECT CloseButtonZone::getButtonRectFrom(const RECT & tabRect, bool isVertical) const
{
	RECT buttonRect;

	int fromBorder;
	if (isVertical)
	{
		fromBorder = (tabRect.right - tabRect.left - _width + 1) / 2;
		buttonRect.left = tabRect.left + fromBorder;
	}
	else
	{
		fromBorder = (tabRect.bottom - tabRect.top - _height + 1) / 2;
		buttonRect.left = tabRect.right - fromBorder - _width;
	}
	buttonRect.top = tabRect.top + fromBorder;
	buttonRect.bottom = buttonRect.top + _height;
	buttonRect.right = buttonRect.left + _width;

	return buttonRect;
}
