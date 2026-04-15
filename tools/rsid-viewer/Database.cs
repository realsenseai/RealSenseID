// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;

namespace rsid_wrapper_csharp
{
    internal class Database
    {

        private static readonly string DefaultPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "db.db");

        public static string GetDatabseDefaultPath()
        {
            return DefaultPath;
        }

        public Database()
        {
            FaceprintsArray = new List<(rsid.Faceprints, string)>();
            DbPath = DefaultPath;
            DbPathSaveBackup = DefaultPath + ".bak";
            DbVersion = -1;
        }

        public Database(String path)
        {
            FaceprintsArray = new List<(rsid.Faceprints, string)>();
            DbPath = path;
            DbPathSaveBackup = path + ".bak";
            DbVersion = -1;
        }

        // PLE - packet layer element
        public bool VerifyVersionMatchedPLE(ref rsid.ExtractedFaceprints faceprints)
        {
            bool versionMatched = ((faceprints.version == DbVersion) || (DbVersion < 0));
            return versionMatched;
        }
        // DBLE - db layer element
        public bool VerifyVersionMatchedDBLE(ref rsid.Faceprints faceprints)
        {
            bool versionMatched = ((faceprints.version == DbVersion) || (DbVersion < 0));
            return versionMatched;
        }

        public bool Push(rsid.Faceprints faceprints, string userId)
        {

            // if DB is empty - set the db version at the first push to the DB.
            if ((DbVersion < 0) && (FaceprintsArray.Count == 0))
            {
                DbVersion = faceprints.version;
            }

            // handle push to db.
            if (DoesUserExist(userId))
            {
                return false;
            }
            else
            {
                FaceprintsArray.Add((faceprints, userId));
                return true;
            }

        }

        public bool DoesUserExist(string userId)
        {
            return (FaceprintsArray.Any(item => item.Item2 == (userId + '\0')));
        }

        public bool Remove(string userId)
        {
            int removedItems = FaceprintsArray.RemoveAll(r => r.Item2 == userId);
            return (removedItems > 0);
        }

        public bool RemoveAll()
        {
            FaceprintsArray.Clear();
            return (FaceprintsArray.Count == 0);
        }

        public void GetUserIds(out string[] userIds)
        {
            int arrayLength = FaceprintsArray.Count;
            userIds = new string[arrayLength];
            for (var i = 0; i < arrayLength; i++)
                userIds[i] = FaceprintsArray[i].Item2;
        }

        public bool UpdateUser(int userIndex, string userIdStr, ref rsid.Faceprints updatedFaceprints)
        {
            bool success = true;

            var userData = FaceprintsArray[userIndex];
            // var userFaceprints = userData.Item1;
            var userIdName = userData.Item2;

            if (userIdStr == userIdName)
            {
                // update by remove and then re-insert (found no other way to do that properly).
                FaceprintsArray.RemoveAt(userIndex);
                FaceprintsArray.Insert(userIndex, (updatedFaceprints, userIdStr));
            }
            else
            {
                Console.WriteLine("Can't update the new faceprints - userName in DB and new vector mismatch!");
                success = false;
            }

            return success;
        }

        public bool SaveBackupAndDeleteDb()
        {
            // this function is called during HandleDbErrorServer() 
            // aiming to handle two possible scenarios :
            //
            // (1) if Faceprints (FP) version changed, e.g. the FP on the db and the current FP changed version (and possibly their internal structure).
            // (2) if db Load() fails on some exception - this may be due to version mismatch (FP structure changed) or other error.
            //
            // in both cases we want to : 
            //
            // (a) backup the old db to a separated file.
            // (b) clear the db and start a new db from scratch.
            // (c) refresh the users list on the gui.
            //      
            bool success = true;

            Console.WriteLine("Saving your old DB to backup file and starting a new DB from scratch.");

            DateTime dt = DateTime.Now;
            string backupPath = DbPathSaveBackup;
            string dtString = dt.ToString("yyyy_MM_dd__HHmmss");
            backupPath += ('_' + dtString);

            Console.WriteLine($"backup (old) DB path = {backupPath}, new (now empty) DB path = {DbPath}");

            try
            {
                // move the DB to backup file, and start a new DB from scratch.
                File.Copy(DbPath, backupPath);
                File.Delete(DbPath);
                RemoveAll();
                Save();
                DbVersion = -1; // clear the version, it will be set on the next Push() to the new DB.
            }
            catch (Exception e)
            {
                Console.WriteLine($"Failed to backup the database at path = {backupPath}. error = {e.Message}");
                success = false;
            }

            return success;
        }

        public void Save()
        {
            try
            {
                DatabaseSerializer.Serialize(FaceprintsArray, DbVersion, DbPath);
            }
            catch (Exception e)
            {
                Console.WriteLine($"Failed saving database at path = {DbPath}. error = {e.Message}");
            }
        }

        public int Load()
        {
            int returnValue = 0;
            try
            {
                Console.WriteLine("Loading database ...");
                if (!File.Exists(DbPath))
                {
                    Console.WriteLine("Database file is missing, using an empty database.");
                    return returnValue;
                }
                FaceprintsArray = DatabaseSerializer.Deserialize(DbPath, out DbVersion);

                if (FaceprintsArray.Count == 0)
                {
                    DbVersion = -1; // must reset dbVersion because Deserialize() may read garbage value from (empty) db file.
                }
                else
                {
                    // check if version mismatch.
                    if (DbVersion != (FaceprintsArray[0].Item1.version))
                    {
                        Console.WriteLine($"DB Load Error: Version Mismatch! DbVersion={DbVersion}, User[0].Version={FaceprintsArray[0].Item1.version}");
                        // will be handled respectively in MainWindow.xaml.cs.
                        returnValue = -1;
                    }
                }
            }
            catch (Exception e)
            {
                Console.WriteLine($"Failed loading database at path = {DbPath}. error = {e.Message}");
                Console.WriteLine("This may be due to change in DB Faceprints version, or some other error.");
                // will be handled respectively in MainWindow.xaml.cs.
                returnValue = -1;
            }

            return returnValue;
        }

        public int GetVersion()
        {
            return DbVersion;
        }

        public List<(rsid.Faceprints, string)> FaceprintsArray;

        // db will be saved to file along with its version number.
        public int DbVersion;

        // path of the db file.    
        public string DbPath;

        // path of backup db file/s.
        // for saving backup of the DB in case of load failure - will save the db to backup file and
        // re-start a new db from scratch. 
        // (e.g. due to Faceprints version change).
        public string DbPathSaveBackup;
    }

}
