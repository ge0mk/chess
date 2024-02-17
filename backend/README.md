# Chess API

## How to Start Chess API

### Start DataBase

Go to the `backend` Folder and execute `docker-compose up` (in future you may also execute `docker start <pid>`).

Verify by checking `localhost:7474`. There should be a website.

### Start API-Server

Go to the `backend/chess_api` Folder and execute `node server.js`.

Verify by checking `localhost:3000`. There should be an `Hello World`.

## API-Commands

### /api/addUser

Adds a new User with the given Username to the system. Returns the Username and the two tokens.

Expect:
```json
{ 
    "userName": "..." 
}
```

Returns:
```json
{
    "userName": "...",
    "publicToken": "...",
    "privateToken": "..."
}
```

### /api/addFriend

Adds a new Friend identified by his public token to the User identified by his private token.

Expect:
```json
{
  "privateToken": "...",
  "publicTokenOfFriend": "..."
}
```

No Json Return.

### /api/changeUserName

Changes the Username of the User identified by his privateToken to the new given userName.

Expect:
```json
{
  "privateToken": "...",
  "userName": "..."
}
```

No Json Return.

### /api/removeFriend

Removes a Friend identified by his public token from the User identified by his private token.

Expect:
```json
{
  "privateToken": "...",
  "publicTokenOfFriend": "..."
}
```

No Json Return.

### /api/getFriends

Returns a list of all friends of the User identified by his private token.

Expect:
```json
{
  "privateToken": "..."
}
```

Returns:
```json
[
  {
    "name": "...",
    "publicToken": "..."
  },
  {
    "name": "...",
    "publicToken": "..."
  },
  ...
]
```