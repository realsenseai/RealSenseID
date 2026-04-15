// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;

namespace rsid_wrapper_csharp
{
    internal class DbObj
    {
        public List<rsid.UserFaceprints> db { get; set; }
        public int version { get; set; }
    }

    internal class DatabaseSerializer
    {
        public static bool Serialize(List<(rsid.Faceprints, string)> users, int db_version, string filename)
        {
            try
            {
                JavaScriptSerializer js = new JavaScriptSerializer();
                js.MaxJsonLength = 1024 * 1024 * 150;
                DbObj json_root = new DbObj();
                List<rsid.UserFaceprints> jsonstring = new List<rsid.UserFaceprints>();
                foreach (var (faceprintsDb, userIdDb) in users)
                {
                    jsonstring.Add(new rsid.UserFaceprints()
                    {
                        userID = userIdDb,
                        faceprints = faceprintsDb
                    });
                }
                json_root.db = jsonstring;
                json_root.version = db_version;
                using (StreamWriter writer = new StreamWriter(filename))
                {
                    writer.WriteLine(js.Serialize(json_root));//.Replace("\\\"", ""));
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Failed serializing database: " + e.Message);
                return false;
            }
            return true;
        }


        public static List<(rsid.Faceprints, string)> Deserialize(string filename, out int db_version)
        {
            try
            {
                using (StreamReader reader = new StreamReader(filename))
                {
                    JavaScriptSerializer js = new JavaScriptSerializer();
                    js.MaxJsonLength = 1024 * 1024 * 150;
                    DbObj obj = js.Deserialize<DbObj>(reader.ReadToEnd());
                    var usr_array = new List<(rsid.Faceprints, string)>();
                    foreach (var uf in obj.db)
                    {
                        usr_array.Add((uf.faceprints, uf.userID));
                    }
                    db_version = obj.version;
                    return usr_array;
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Failed deserializing database: " + e.Message);
                db_version = -1;
                return null;
            }
        }
    }
}