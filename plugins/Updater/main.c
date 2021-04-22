/*
 * Process Hacker Plugins -
 *   Update Checker Plugin
 *
 * Copyright (C) 2011-2019 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "updater.h"

PPH_PLUGIN PluginInstance;
PH_CALLBACK_REGISTRATION PluginMenuItemCallbackRegistration;
PH_CALLBACK_REGISTRATION MainMenuInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION MainWindowShowingCallbackRegistration;
PH_CALLBACK_REGISTRATION PluginShowOptionsCallbackRegistration;

VOID NTAPI MainWindowShowingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    // Check if the user want's us to auto-check for updates.
    if (PhGetIntegerSetting(SETTING_NAME_AUTO_CHECK))
    {
        // All good, queue up our update check.
        StartInitialCheck();
    }
}

VOID NTAPI MainMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = Parameter;

    // Check this menu is the Help menu
    if (!menuInfo || menuInfo->u.MainMenu.SubMenuIndex != PH_MENU_ITEM_LOCATION_HELP)
        return;

    PhInsertEMenuItem(menuInfo->Menu, PhPluginCreateEMenuItem(PluginInstance, 0, UPDATE_MENUITEM, L"Check for &updates", NULL), 0);
}

VOID NTAPI MenuItemCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_ITEM menuItem = Parameter;

    if (menuItem && menuItem->Id == UPDATE_MENUITEM)
    {
        ShowUpdateDialog(NULL);
    }
}

VOID NTAPI ShowOptionsCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_OPTIONS_POINTERS optionsEntry = (PPH_PLUGIN_OPTIONS_POINTERS)Parameter;

    if (!optionsEntry)
        return;

    optionsEntry->CreateSection(
        L"Updater",
        PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_OPTIONS),
        OptionsDlgProc,
        NULL
        );
}

LOGICAL DllMain(
    _In_ HINSTANCE Instance,
    _In_ ULONG Reason,
    _Reserved_ PVOID Reserved
    )
{
    switch (Reason)
    {
    case DLL_PROCESS_ATTACH:
        {
            PPH_PLUGIN_INFORMATION info;
            PH_SETTING_CREATE settings[] =
            {
                { IntegerSettingType, SETTING_NAME_AUTO_CHECK, L"1" },
                { StringSettingType, SETTING_NAME_LAST_CHECK, L"0" },
                { IntegerPairSettingType, SETTING_NAME_CHANGELOG_WINDOW_POSITION, L"0,0" },
                { ScalableIntegerPairSettingType, SETTING_NAME_CHANGELOG_WINDOW_SIZE, L"@96|420,250" },
                { StringSettingType, SETTING_NAME_CHANGELOG_COLUMNS, L"" },
                { StringSettingType, SETTING_NAME_CHANGELOG_SORTCOLUMN, L"" },
            };

            PluginInstance = PhRegisterPlugin(PLUGIN_NAME, Instance, &info);

            if (!PluginInstance)
                return FALSE;

            info->DisplayName = L"Update Checker";
            info->Author = L"dmex";
            info->Description = L"Plugin for checking new Process Hacker releases via the Help menu.";
            info->Url = L"https://wj32.org/processhacker/forums/viewtopic.php?t=1121";

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackMainWindowShowing),
                MainWindowShowingCallback,
                NULL,
                &MainWindowShowingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackMainMenuInitializing),
                MainMenuInitializingCallback,
                NULL,
                &MainMenuInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackMenuItem),
                MenuItemCallback,
                NULL,
                &PluginMenuItemCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackOptionsWindowInitializing),
                ShowOptionsCallback,
                NULL,
                &PluginShowOptionsCallbackRegistration
                );

            PhAddSettings(settings, RTL_NUMBER_OF(settings));
        }
        break;
    }

    return TRUE;
}
