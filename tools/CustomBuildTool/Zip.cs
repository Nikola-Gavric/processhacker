﻿/*
 * Process Hacker Toolchain - 
 *   Build script
 * 
 * Copyright (C) dmex
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

using System;
using System.IO;
using System.IO.Compression;

namespace CustomBuildTool
{
    public static class Zip
    {
        // https://github.com/phofman/zip/blob/master/src/ZipFile.cs
        private static string[] GetEntryNames(string[] names, string sourceFolder, bool includeBaseName)
        {
            if (names == null || names.Length == 0)
                return new string[0];

            if (includeBaseName)
                sourceFolder = Path.GetDirectoryName(sourceFolder);

            int length = string.IsNullOrEmpty(sourceFolder) ? 0 : sourceFolder.Length;
            if (length > 0 && sourceFolder != null && sourceFolder[length - 1] != Path.DirectorySeparatorChar && sourceFolder[length - 1] != Path.AltDirectorySeparatorChar)
                length++;

            var result = new string[names.Length];
            for (int i = 0; i < names.Length; i++)
            {
                result[i] = names[i].Substring(length);
            }

            return result;
        }

        public static void CreateCompressedFolder(string sourceDirectoryName, string destinationArchiveFileName)
        {
            string[] filesToAdd = Directory.GetFiles(sourceDirectoryName, "*", SearchOption.AllDirectories);
            string[] entryNames = GetEntryNames(filesToAdd, sourceDirectoryName, false);

            if (File.Exists(destinationArchiveFileName))
                File.Delete(destinationArchiveFileName);

            using (FileStream zipFileStream = new FileStream(destinationArchiveFileName, FileMode.Create))
            using (ZipArchive archive = new ZipArchive(zipFileStream, ZipArchiveMode.Create, true))
            {
                for (int i = 0; i < filesToAdd.Length; i++)
                {
                    string file = filesToAdd[i];
                    string name = entryNames[i];

                    if (
                       file.StartsWith("bin\\Debug", StringComparison.OrdinalIgnoreCase) || // Ignore junk files
                       file.EndsWith(".pdb", StringComparison.OrdinalIgnoreCase) ||
                       file.EndsWith(".iobj", StringComparison.OrdinalIgnoreCase) ||
                       file.EndsWith(".ipdb", StringComparison.OrdinalIgnoreCase) ||
                       file.EndsWith(".exp", StringComparison.OrdinalIgnoreCase) ||
                       file.EndsWith(".lib", StringComparison.OrdinalIgnoreCase)
                       )
                    {
                        continue;
                    }

                    if (name.StartsWith("Release32\\", StringComparison.OrdinalIgnoreCase))
                        name = name.Replace("Release32\\", "32bit\\", StringComparison.OrdinalIgnoreCase);
                    if (name.StartsWith("Release64\\", StringComparison.OrdinalIgnoreCase))
                        name = name.Replace("Release64\\", "64bit\\", StringComparison.OrdinalIgnoreCase);

                    archive.CreateEntryFromFile(file, name, CompressionLevel.Optimal);
                }
            }

            //using (var filestream = File.Create(destinationArchiveFileName))
            //using (var archive = new SevenZipArchive(filestream, FileAccess.Write))
            //using (var compressor = archive.Compressor())
            //{
            //    compressor.CompressHeader = true;
            //    compressor.PreserveDirectoryStructure = true;
            //    compressor.Solid = true;
            //
            //    for (int i = 0; i < filesToAdd.Length; i++)
            //    {
            //        string file = filesToAdd[i];
            //
            //        if (
            //           file.StartsWith("Debug", StringComparison.OrdinalIgnoreCase) ||// Ignore directories (Debug32/Debug64)
            //           file.EndsWith(".pdb", StringComparison.OrdinalIgnoreCase) ||  // Ignore files
            //           file.EndsWith(".iobj", StringComparison.OrdinalIgnoreCase) ||
            //           file.EndsWith(".ipdb", StringComparison.OrdinalIgnoreCase) ||
            //           file.EndsWith(".exp", StringComparison.OrdinalIgnoreCase) ||
            //           file.EndsWith(".lib", StringComparison.OrdinalIgnoreCase)
            //           )
            //        {
            //            continue;
            //        }
            //
            //        var name = file.Replace(sourceDirectoryName, string.Empty);
            //        compressor.AddFile(file, name);
            //    }
            //
            //    compressor.Finalize();
            //}
        }

        public static void CreateCompressedSdkFromFolder(string sourceDirectoryName, string destinationArchiveFileName)
        {
            string[] filesToAdd = Directory.GetFiles(sourceDirectoryName, "*", SearchOption.AllDirectories);
            string[] entryNames = GetEntryNames(filesToAdd, sourceDirectoryName, false);

            if (File.Exists(destinationArchiveFileName))
                File.Delete(destinationArchiveFileName);

            using (FileStream zipFileStream = new FileStream(destinationArchiveFileName, FileMode.Create))
            using (ZipArchive archive = new ZipArchive(zipFileStream, ZipArchiveMode.Create, true))
            {
                for (int i = 0; i < filesToAdd.Length; i++)
                {
                    archive.CreateEntryFromFile(filesToAdd[i], entryNames[i], CompressionLevel.Optimal);
                }
            }

            //string[] filesToAdd = Directory.GetFiles(sourceDirectoryName, "*", SearchOption.AllDirectories);
            //
            //if (File.Exists(destinationArchiveFileName))
            //    File.Delete(destinationArchiveFileName);
            //
            //using (var filestream = File.Create(destinationArchiveFileName))
            //using (var archive = new SevenZipArchive(filestream, FileAccess.Write))
            //using (var compressor = archive.Compressor())
            //{
            //    compressor.CompressHeader = true;
            //    compressor.PreserveDirectoryStructure = true;
            //    compressor.Solid = true;
            //
            //    for (int i = 0; i < filesToAdd.Length; i++)
            //    {
            //        string file = filesToAdd[i];
            //        var name = file.Replace(sourceDirectoryName, string.Empty);
            //
            //        compressor.AddFile(file, name);
            //    }
            //
            //    compressor.Finalize();
            //}
        }

        public static void CreateCompressedPdbFromFolder(string sourceDirectoryName, string destinationArchiveFileName)
        {
            string[] filesToAdd = Directory.GetFiles(sourceDirectoryName, "*", SearchOption.AllDirectories);
            string[] entryNames = GetEntryNames(filesToAdd, sourceDirectoryName, false);

            if (File.Exists(destinationArchiveFileName))
                File.Delete(destinationArchiveFileName);

            using (FileStream zipFileStream = new FileStream(destinationArchiveFileName, FileMode.Create))
            using (ZipArchive archive = new ZipArchive(zipFileStream, ZipArchiveMode.Create, true))
            {
                for (int i = 0; i < filesToAdd.Length; i++)
                {
                    // Ignore junk files
                    if (!filesToAdd[i].EndsWith(".pdb", StringComparison.OrdinalIgnoreCase))
                        continue;

                    // Ignore junk directories
                    if (filesToAdd[i].Contains("bin\\Debug") ||
                        filesToAdd[i].Contains("obj\\") ||
                        filesToAdd[i].Contains("tests\\"))
                        continue;

                    archive.CreateEntryFromFile(filesToAdd[i], entryNames[i], CompressionLevel.Optimal);
                }
            }
        }
    }
}
