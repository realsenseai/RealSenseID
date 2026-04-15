// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.db;

import androidx.room.Dao;
import androidx.room.Delete;
import androidx.room.Insert;
import androidx.room.Query;
import androidx.room.Update;
import java.util.List;

@Dao
public interface UserDao {
  @Query("SELECT * FROM user")
  List<User> getAll();

  @Insert
  void insertUser(User user);

  @Update
  void updateUser(User user);

  @Insert
  void insertAll(User... users);

  @Delete
  void delete(User user);
}
