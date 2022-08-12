# API Documentation

# API
Uses POST / GET requests to gain information from IDHAN.


## Adding files
### **POST `/files/add_file`**

_Informs the client to import/process a file_

Content Type:
: `application/json`

**REQUIRED** Arguments:
: `path`: The **FULL** path to import

*optional* Arguments:
: `tags`: A encoded list of tags to add to the imported image (If the image already exists these will just be appended)

Example:
```json
{
	"path": "/home/kj16609/Desktop/ToImport/abunchofcatgirls.jpg"
}
```

Response:
: Note that this will only respond upon the file being fully written to disk and imported

```json
{
	"result": 1,
	"sha256": "627a26d48ebebb98cd9f134119e90fe13effc47031e51fbc81daa99526eb03b4",
	"info":
}
```

`result`:
```
1 - File was imported successfully
2 - File was already present
3 - File previously deleted
4 - File failed to import
5 - Blacklisted - Hash
6 - Blacklisted - Tag 
```

`info`: Will contain information about failure conditions (Such as which tags were in the blacklist)

---

Markings
---
Markings allow for a 3rd party application to create custom flags/information that is stored within the database

#
### **POST `/markings/create_marking`**

_Creates a new marking to use_

Content Type:
: `application/json`

REQUIRED Arguments:
: `marking_nane`: Name of the marker to created
: `marking_dataype`: One of the valid datatypes defined by the table below

OPTIONAL Arguments:
: `prefill`: True/False if true then create a row for each file already in the database with `data`
: `data`: (See `POST /markings/set_marking` for how to format this)

Note: Prefill is not recommended since it could take a VERY long time to complete. Unless you are ABSOLUTELY sure you need it (Doubtful but i'm adding it anyways) don't use it


| Type         | ByteSize | C++ Equiv        |
|--------------|----------|------------------|
| bool         | 1        | bool             |
| int          | 4        | int32_t          |
| uint         | 4        | uint32_t         |
| longint      | 8        | int64_t          |
| ulongint     | 8        | uint64_t         |
| text         | ?        | std::string      |
| array\<type> | ?        | std::vector\<T>  |

Note: Arrays can be used aswell with any of the previous types in the table but they are expensive since you'll have to re-write the entire array in order to append/delete even a single item in the array


### NOTE ABOUT SECURITY! READ THIS. YES WITH YOUR EYES. The 'key' you get when you use this is NOT SECURE. It is JUST to prevent someone from coming in and deleting your markers by accident. BUT IF YOU LOSE THIS KEY (Fucking idiot) you will be unable to DELETE the marker via the GET request and instead will have to delete it by directly accessing the database (Don't fucking do it). STORE IT SOMEWHERE IN YOUR APPLICATION
Additional note: I **WILL NOT** and _**AM NOT**_ adding more 'protection' to markers then this. If some other dev overwrites or fucks up your marker then go shit in their cereal not mine.
Best practice is to name your markers MYAPPNAME_markername. For example HYDOWNLOADER_importtime or something similar. Do ThIs_FoR_aLl_I_cArE

_**Example Result:**_
```json
{
  "status": 1,
  "delete_key": "f007783c52770565516995e457dbed5029825326cb41b653ca5949bbedcc2744"
}
```

`status`:

| id  | reason                                                           |
|-----|------------------------------------------------------------------|
| 1   | Success                                                          |
| 2   | Naming conflict (Name already exists)                            |
| 3   | Invalid type                                                     |
| 4   | Prefill failed (See `POST /markings/set_marking` for more info)  |



## Table of pre-existing markings that cannot be used by a 3rd-party
| Name            | Type | Description                                                                                                            |
|-----------------|------|------------------------------------------------------------------------------------------------------------------------|
| trashed         | bool | File will be deleted when possible                                                                                     |
| deleted         | bool | File no longer exists on disk                                                                                          |
| deleted_reason  | text | Reason for a deletion                                                                                                  |
| mappings_purged | bool | Allows/disallows new mappings to be made and setting to true deletes all existing mappings (Used for 'hard blacklist') |
| file_purged     | bool | Disallows importing of the file again                                                                                  |

### Mappings in use by IDHAN-GUI
| Name | Type | Description  |
|------|------|--------------|




### **GET `/markings/get_markings`**

_Gets markings with NAME for a specific file id_

Content Type:
: `application/json`

**REQUIRED** Arguments:
: `file_id`: id of the file
: `marker_name`: Name of the marker you wish to access

_**Example Result:**_

```json
{
	"trashed": true
}
```

Note: In the event that a row doesn't exist in the database yet for the requested data then it will return `null` instead.

#
### **POST `/markings/set_marking`**

_Sets a marking for NAME on a file_

Content Type:
: `application/json`

REQUIRED Arguments:
: `file_id`: ID of file to apply marker too
: `marking_nane`: Name of the marker
: `data`: data to insert/apply (For text surround the paramter in quotes e.g. data="MyTextHere")

_**Example Result**_

```json
{
  "status_id": 1,
  "message": ""
}
```

| status_id  | reason                   |
|------------|--------------------------|
| 1          | success                  |
| 2          | data does not match type |
| 3          | invalid marker_name      |
`message` will be populated with an error message upon failure













