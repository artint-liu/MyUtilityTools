#pragma once

#define IDR_MANIFEST 1
#define IDI_APP_ICON 101

// Menu ids
#define IDM_FILE_OPEN       1001
#define IDM_FILE_EXIT       1002
#define IDM_VIEW_TOC        1003
#define IDM_FILE_SAVE_HTML  1004
#define IDM_EDIT_FIND       1005
#define IDM_EDIT_FIND_NEXT  1006
#define IDM_EDIT_FIND_PREV  1007

// 最近打开文件菜单占位（点击动态插入，故预留一段 id 区间）
#define IDM_RECENT_FIRST    1100
#define IDM_RECENT_LAST     1199   // 最多 100 条（注册表最多 50 条，足够）
#define IDM_FILE_CLEAR_RECENT 1200
#define IDM_FILE_ESC_EXIT   1201   // ESC 退出选项（勾选框）

// 搜索栏控件 id
#define IDC_SEARCH_EDIT     2001
#define IDC_SEARCH_CASE     2002
#define IDC_SEARCH_LABEL    2003
#define IDC_SEARCH_PREV     2004
#define IDC_SEARCH_NEXT     2005
#define IDC_SEARCH_CLOSE    2006
