# minqlx - Extends Quake Live's dedicated server with extra functionality and scripting.
# Copyright (C) 2026 Paul Asalgado <locopol@gmail.com>

# This file is part of minqlx.

# minqlx is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# minqlx is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with minqlx. If not, see <http://www.gnu.org/licenses/>.

import os
import shelve
import threading
import minqlx

# ====================================================================
#                          AbstractDatabase
# ====================================================================
class AbstractDatabase:
    # An instance counter. Useful for closing connections.
    _counter = 0

    def __init__(self, plugin):
        self.plugin = plugin
        self.__class__._counter += 1

    def __del__(self):
        self.__class__._counter -= 1

    @property
    def logger(self):
        return minqlx.get_logger(self.plugin)

    def set_permission(self, player):
        """Abstract method. Should set the permission of a player.

        :raises: NotImplementedError

        """
        raise NotImplementedError("The base plugin can't do database actions.")

    def get_permission(self, player):
        """Abstract method. Should return the permission of a player.

        :returns: int
        :raises: NotImplementedError

        """
        raise NotImplementedError("The base plugin can't do database actions.")

    def has_permission(self, player, level=5):
        """Abstract method. Should return whether or not a player has more than or equal
        to a certain permission level. Should only take a value of 0 to 5, where 0 is
        always True.

        :returns: bool
        :raises: NotImplementedError

        """
        raise NotImplementedError("The base plugin can't do database actions.")

    def set_flag(self, player, flag, value=True):
        """Abstract method. Should set specified player flag to value.

        :raises: NotImplementedError

        """
        raise NotImplementedError("The base plugin can't do database actions.")

    def clear_flag(self, player, flag):
        """Should clear specified player flag."""
        return self.set_flag(player, flag, False)

    def get_flag(self, player, flag, default=False):
        """Abstract method. Should return specified player flag

        :returns: bool
        :raises: NotImplementedError

        """
        raise NotImplementedError("The base plugin can't do database actions.")

    def connect(self):
        """Abstract method. Should return a connection to the database. Exactly what a
        "connection" obviously depends on the database, so the specifics will be up
        to the implementation.

        A :class:`minqlx.Plugin` subclass can set

        :raises: NotImplementedError

        """
        raise NotImplementedError("The base plugin can't do database actions.")

    def close(self):
        """Abstract method. If the database has a connection state, this method should
        close the connection.

        :raises: NotImplementedError

        """
        raise NotImplementedError("The base plugin can't do database actions.")

class Redis(AbstractDatabase):
    """
    Symmetric abstraction class that emulates Redis using 
    Python's native persistent shelve Embed engine.
    """
    def __init__(self, filename="minqlx_db.dir"):
        # isolate threading lock
        self.lock = threading.Lock()
        
        # Relative path of game instance (multiple servers?, WIP)
        #os.path.join(minqlx.get_cvar("fs_homepath"))
        base_dir = os.path.dirname(os.path.abspath(__file__))

        # We backtrack if we are inside minqlx.zip or the subfolder
        if ".zip" in base_dir.lower() or "minqlx" in base_dir.lower():
            base_dir = "."
            
        self.db_path = os.path.join(base_dir, "minqlx_data")
        
        # Initialize storage container on disk
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                if "_keys_master" not in db:
                    db["_keys_master"] = {}

    # Redis compatible commands (strings)
    
    def get(self, key):
        with self.lock:
            with shelve.open(self.db_path) as db:
                master = db.get("_keys_master", {})
                return master.get(str(key), None)

    def set(self, key, value):
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                master = db["_keys_master"]
                master[str(key)] = str(value)
                db["_keys_master"] = master
        return True

    def exists(self, key):
        with self.lock:
            with shelve.open(self.db_path) as db:
                master = db.get("_keys_master", {})
                return str(key) in master

    def delete(self, *keys):
        count = 0
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                master = db["_keys_master"]
                for key in keys:
                    str_key = str(key)
                    if str_key in master:
                        del master[str_key]
                        count += 1
                    # If it also happens to be a composite hash, we clean the root table.
                    if str_key in db:
                        del db[str_key]
                db["_keys_master"] = master
        return count

    def lpush(self, key, *values):
        """
        Insert one or more elements on db.
        Simula con precisión el comando LPUSH de Redis.
        """
        str_key = str(key)
        
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                # initialize if not exists
                if str_key not in db:
                    db[str_key] = []
                
                # force list
                current_list = list(db[str_key])
                
                #inserts in reverse order if multiple arguments are passed, so that the last value is at absolute index 0.
                for value in values:
                    current_list.insert(0, str(value))
                
                # write changes
                db[str_key] = current_list
                
                # Return length of list
                return len(current_list)

    def incr(self, key):
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                master = db["_keys_master"]
                str_key = str(key)
                current = int(master.get(str_key, 0))
                current += 1
                master[str_key] = str(current)
                db["_keys_master"] = master
                return current

    # Redis compatible commands (hashes)

    def hmset(self, key, mapping):
        str_key = str(key)
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                db[str_key] = dict(mapping)
        return True
    
    def hset(self, name, key, value):
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                str_name = str(name)
                str_key = str(key)
                if str_name not in db:
                    db[str_name] = {}
                hash_map = db[str_name]
                hash_map[str_key] = str(value)
                db[str_name] = hash_map
        return True

    def hget(self, name, key):
        with self.lock:
            with shelve.open(self.db_path) as db:
                str_name = str(name)
                if str_name not in db:
                    return None
                return db[str_name].get(str(key), None)

    def hgetall(self, name):
        with self.lock:
            with shelve.open(self.db_path) as db:
                str_name = str(name)
                if str_name not in db:
                    return {}
                # Returns a clean copy of the internal dictionary in RAMs
                return dict(db[str_name])

    def hdel(self, name, *keys):
        count = 0
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                str_name = str(name)
                if str_name in db:
                    hash_map = db[str_name]
                    for key in keys:
                        str_key = str(key)
                        if str_key in hash_map:
                            del hash_map[str_key]
                            count += 1
                    db[str_name] = hash_map
        return count

    def hkeys(self, name):
        with self.lock:
            with shelve.open(self.db_path) as db:
                str_name = str(name)
                if str_name not in db:
                    return []
                return list(db[str_name].keys())

    def hlen(self, name):
        with self.lock:
            with shelve.open(self.db_path) as db:
                str_name = str(name)
                if str_name not in db:
                    return 0
                return len(db[str_name])

    def hexists(self, name, key):
        with self.lock:
            with shelve.open(self.db_path) as db:
                str_name = str(name)
                if str_name not in db:
                    return False
                return str(key) in db[str_name]

    # Redis compatible commands (Sorted Sets)

    def zcard(self, key):
        str_key = str(key)
        with self.lock:
            with shelve.open(self.db_path) as db:
                if str_key not in db: return 0
                return len(db[str_key])

    def zadd(self, key, *args):
        """Map operator supporting both old (score, member) and modern {member: score} syntax"""
        str_key = str(key)
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                if str_key not in db:
                    db[str_key] = {}
                sorted_set = db[str_key]

                #Analize argument variant according of ban.py
                if len(args) == 1 and isinstance(args[0], dict):
                    for member, score in args[0].items():
                        sorted_set[str(member)] = float(score)
                elif len(args) >= 2:
                    # classic format zadd(key, score, member)
                    score = args[0]
                    member = args[1]
                    sorted_set[str(member)] = float(score)

                db[str_key] = sorted_set
        return 1

    def zrangebyscore(self, key, min_score, max_score, withscores=False):
        """Emulates timestamp filter search for expired bans"""
        str_key = str(key)
        results = []
        
        with self.lock:
            with shelve.open(self.db_path) as db:
                if str_key not in db: return []
                sorted_set = db[str_key]
                
                # pass "+inf" from the original script
                float_max = float('inf') if max_score == "+inf" else float(max_score)
                float_min = float(min_score)

                for member, score in sorted_set.items():
                    if float_min <= score <= float_max:
                        results.append((member, score) if withscores else member)
                
                # sort by the score (expiration timestamp)
                results.sort(key=lambda x: x[1] if withscores else sorted_set[x])
                return results

    def zincrby(self, key, amount, value):
        """Emulates incremental amount of value"""
        str_key = str(key)
        str_val = str(value)
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                if str_key in db:
                    sorted_set = db[str_key]
                    if str_val in sorted_set:
                        sorted_set[str_val] += float(amount)
                        db[str_key] = sorted_set
        return 1

    # Compatibility Operators 
    
    def keys(self, pattern="*"):
        """
        Transform simple Redis-style patterns (e.g., "player:*") into Python filters
        """
        with self.lock:
            with shelve.open(self.db_path) as db:
                master = db.get("_keys_master", {})
                all_keys = list(master.keys()) + [k for k in db.keys() if k != "_keys_master"]
                
                clean_pattern = pattern.replace("*", "")
                if pattern == "*":
                    return all_keys
                return [k for k in all_keys if clean_pattern in k]

    def __delitem__(self, key):
        str_key = str(key)
        res = self.delete(str_key)
        if res == 0:
            raise KeyError("The key '{}' is not present in the database.".format(key))

    def __getitem__(self, key):
        """
        Maps the reading by brackets to self.db[key].
        Symmetrical logic of .get() method but throwing KeyError if it doesn't exist,
        satisfying the original try/except block of motd.py plugin.
        """
        str_key = str(key)
        with self.lock:
            with shelve.open(self.db_path) as db:
                master = db.get("_keys_master", {})
                if str_key not in master:
                    raise KeyError(str_key)
                return master[str_key]

    def __setitem__(self, key, value):
        """
        Map the write to square brackets: like self.db[key] = value.
        Transparently redirect the string to the central store.
        """
        self.set(key, value)

    def __contains__(self, key):
        """
        Map the membership operator: if key in self.db:
        """
        return self.exists(key)

    def sadd(self, key, *members):
        """
        Adds members to a persistent set indivisibly like Redis operator
        """
        str_key = str(key)
        added_count = 0
        
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                # If the key does not exist in storage, we initialize a clean Python set.
                if str_key not in db:
                    db[str_key] = set()
                
                # Extract set from RAM
                current_set = db[str_key]
                
                # We ensure it's a set-type object.
                if not isinstance(current_set, set):
                    current_set = set(current_set) if isinstance(current_set, (list, tuple)) else set()

                for member in members:
                    str_member = str(member)
                    if str_member not in current_set:
                        current_set.add(str_member)
                        added_count += 1
                
                # Reinject the mutated set into storage.
                db[str_key] = current_set
                
        return added_count

    def sismember(self, key, member):
        """
        Checks if a member belongs to the set.
        """
        str_key = str(key)
        str_member = str(member)
        
        with self.lock:
            with shelve.open(self.db_path) as db:
                if str_key not in db:
                    return 0
                current_set = db[str_key]
                return 1 if str_member in current_set else 0

    def smembers(self, key):
        """
        Returns all elements of the set.
        Returns a list of strings to maintain symmetry with the original operator.
        """
        str_key = str(key)
        
        with self.lock:
            with shelve.open(self.db_path) as db:
                if str_key not in db:
                    return []
                current_set = db[str_key]
                return list(current_set)

    def srem(self, key, *members):
        """
        Remove members from the set, used by clearmotd o removemotd.
        """
        str_key = str(key)
        removed_count = 0
        
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                if str_key in db:
                    current_set = db[str_key]
                    for member in members:
                        str_member = str(member)
                        if str_member in current_set:
                            current_set.remove(str_member)
                            removed_count += 1
                    db[str_key] = current_set
        return removed_count
    
    # minqlx legacy functions

    def set_permission(self, player, level):
        """Sets the permission of a player.

        :param player: The player in question.
        :type player: minqlx.Player

        """
        if isinstance(player, minqlx.Player):
            key = "minqlx:players:{}:permission".format(player.steam_id)
        else:
            key = "minqlx:players:{}:permission".format(player)

        str_level = str(int(level))

        # Protect file isolation against concurrent access from network hooks
        with self.lock:
            with shelve.open(self.db_path, writeback=True) as db:
                db[key] = str_level
                
                if "_keys_master" not in db:
                    db["_keys_master"] = {}
                db["_keys_master"][key] = str_level

    def get_permission(self, player):
        """Gets the permission of a player.

        :param player: The player in question.
        :type player: minqlx.Player, int
        :returns: int

        """
        if isinstance(player, minqlx.Player):
            steam_id = player.steam_id
        elif isinstance(player, int):
            steam_id = player
        elif isinstance(player, str):
            steam_id = int(player)
        else:
            raise ValueError("Invalid player. Use either a minqlx.Player instance or a SteamID64.")

        # If it's the owner, treat it like a 5.
        if steam_id == minqlx.owner():
            return 5

        key = "minqlx:players:{}:permission".format(steam_id)
        perm = "0"

        # Protect file isolation against concurrent access from network hooks
        with self.lock:
            with shelve.open(self.db_path) as db:
                if key in db:
                    perm = db[key]
                elif "_keys_master" in db and key in db["_keys_master"]:
                    perm = db["_keys_master"][key]

        try:
            return int(perm)
        except (ValueError, TypeError):
            return 0

    def has_permission(self, player, level=5):
        """Checks if the player has higher than or equal to *level*.

        :param player: The player in question.
        :type player: minqlx.Player
        :param level: The permission level to check for.
        :type level: int
        :returns: bool

        """
        return self.get_permission(player) >= level

    def get_flag(self, player, flag, default=False):
        """Clears the specified player flag

        :param player: The player in question.
        :type player: minqlx.Player
        :param flag: The flag to get
        :type flag: string
        :param default: (optional, default=False) The value to return if the flag is unknown
        :type default: bool

        """
        if isinstance(player, minqlx.Player):
            key = "minqlx:players:{0}:flags:{1}".format(player.steam_id, flag)
        else:
            key = "minqlx:players:{0}:flags:{1}".format(player, flag)
        
        try:
            return bool(int(self[key]))
        except KeyError:
            return default

    def set_flag(self, player, flag, value=True):
        """Sets specified player flag

        :param player: The player in question.
        :type player: minqlx.Player
        :param flag: The flag to set.
        :type flag: string
        :param value: (optional, default=True) Value to set
        :type value: bool

        """
        if isinstance(player, minqlx.Player):
            key = "minqlx:players:{0}:flags:{1}".format(player.steam_id, flag)
        else:
            key = "minqlx:players:{0}:flags:{1}".format(player, flag)
        
        # __setitem__ Redis like
        self[key] = "1" if value else "0"

    def pipeline(self):
        """Return simulated instance of Redis"""
        return ShelvePipeline(self)
        
class ShelvePipeline:
    """
    A mirror class that emulates a Redis Pipeline.
    It stores commands in a queue and executes them atomically on disk.
    This is a list commands identified in minqlx core python routines.
    """
    def __init__(self, db_instance):
        self.db = db_instance
        self.queue = []
    def sadd(self, key, *members):
        self.queue.append(('sadd', (key, *members)))
        return self

    def set(self, key, value):
        self.queue.append(('set', (key, value)))
        return self        

    def incr(self, key):
        self.queue.append(('incr', (key,)))
        return self

    def zadd(self, key, *args):
        self.queue.append(('zadd', (key, *args)))
        return self

    def hmset(self, key, mapping):
        self.queue.append(('hmset', (key, mapping)))
        return self

    def zincrby(self, key, amount, value):
        self.queue.append(('zincrby', (key, amount, value)))
        return self

    def lpush(self, key, *values):
        self.queue.append(('lpush', (key, *values)))
        return self

    def execute(self):
        # We dispatch all accumulated commands under a single lock
        results = []
        for cmd, args in self.queue:
            method = getattr(self.db, cmd)
            results.append(method(*args))
        self.queue = []
        return results

# Minqlx init 
class Database(Redis):
    def __init__(self):
        super().__init__()