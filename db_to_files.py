#!/usr/bin/python

from pymongo import Connection

def convert():
    connection = Connection()
    db = connection.prod
    songs = db.songs

    fp = file("stations_songs.txt", 'w')
    delimiter = "---"

    stations = songs.distinct("station")
    for station in stations:
        print >>fp, delimiter
        print >>fp, station
        for song in songs.find({"station": station, "has_uri": 1}):
            print >>fp, song['uri']

if __name__ == "__main__":
    convert()
            
