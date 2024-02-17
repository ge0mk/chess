const neo4j = require('neo4j-driver')
const driver = neo4j.driver('neo4j://localhost:7687', neo4j.auth.basic('neo4j', 'password'))
const session = driver.session()

const tokenCharacters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
const userNameCharacters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
const privateTokenLength = 10;
const publicTokenLength = 10;

// FriendList - Functions ----------------------------------------------------------------------------------------------
async function addUser(username) {

    if (!containsOnlyCharset(username, userNameCharacters)) throw new Error("13");

    const publicToken = await generatePublicToken();
    const privateToken = await generatePrivateToken();

    try {
        const result = await session.writeTransaction(async (tx) => {
            await tx.run('CREATE (:User {name: $userName, publicToken: $publicToken, privateToken: $privateToken })', { userName: username, publicToken: publicToken, privateToken: privateToken})
        });
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }

    return {
        name: username,
        publicToken: publicToken,
        privateToken: privateToken
    }
}

async function addFriend(privateToken, publicTokenOfFriend) {
    if (!(await validatePublicToken(publicTokenOfFriend))) throw new Error("10");
    if (!(await validatePrivateToken(privateToken))) throw new Error("11");
    if ((await getPublicTokenForPrivateToken(privateToken)) === publicTokenOfFriend) throw new Error("12");
    if ((await checkIfTheyHaveAnFriendship(privateToken, publicTokenOfFriend))) throw new Error("16");

    try {
        await session.writeTransaction(async (tx) => {
            const query = 'MATCH (You:User {privateToken: $privateToken}), (Friend:User {publicToken: $publicToken})' +
                'MERGE (You)-[:Friend]->(Friend)';
            const params = { privateToken: privateToken, publicToken: publicTokenOfFriend };
            await tx.run(query, params);
        });
        return true;
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

async function changeUserName(privateToken, newUsername) {
    if (!(await validatePrivateToken(privateToken))) throw new Error("11");
    if (!containsOnlyCharset(newUsername, userNameCharacters)) throw new Error("13");
    if ((await getUserNameForPrivateToken(privateToken)) === newUsername) throw new Error("14");

    try {
        await session.writeTransaction(async (tx) => {
            const query = 'MATCH (n:User {privateToken: $privateToken}) SET n.name = $newUserName';
            const params = { privateToken: privateToken, newUserName: newUsername };
            await tx.run(query, params);
        });
        return true;
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

async function removeFriend(privateToken, publicTokenOfFriend) {
    if (!(await validatePublicToken(publicTokenOfFriend))) throw new Error("10");
    if (!(await validatePrivateToken(privateToken))) throw new Error("11");
    if ((await getPublicTokenForPrivateToken(privateToken)) === publicTokenOfFriend) throw new Error("12");
    if (!(await checkIfTheyHaveAnFriendship(privateToken, publicTokenOfFriend))) throw new Error("15");

    try {
        await session.writeTransaction(async (tx) => {
            const query = 'MATCH (You:User {privateToken: $privateToken})-[a:Friend]->(Friend:User {publicToken: $publicToken})' +
                'DELETE a';
            const params = { privateToken: privateToken, publicToken: publicTokenOfFriend };
            await tx.run(query, params);
        });
        return true;
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

async function getFriends(privateToken){
    if (!(await validatePrivateToken(privateToken))) throw new Error("11");

    try {
        return await session.readTransaction(async (tx) => {
            const query = 'MATCH (:User {privateToken: $privateToken})-[:Friend]->(friend:User)' +
                'RETURN friend.name, friend.publicToken;';
            const params = {privateToken: privateToken};
            const {records} = await tx.run(query, params);

            return records.map(record => {
                return {
                    name: record.get('friend.name'),
                    publicToken: record.get('friend.publicToken')
                };
            });
        });
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

// Util ----------------------------------------------------------------------------------------------------------------
async function checkIfPublicTokenExists(publicToken) {
    try {
        const result = await session.readTransaction(async (tx) => {
            const query = 'MATCH (n:User {publicToken: $publicToken}) RETURN count(n) AS count';
            const params = { publicToken: publicToken };
            const { records } = await tx.run(query, params);
            return records[0].get('count') > 0;
        });

        return result > 0;
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

async function checkIfPrivateTokenExists(privateToken) {
    try {
        const result = await session.readTransaction(async (tx) => {
            const query = 'MATCH (n:User {privateToken: $privateToken}) RETURN count(n) AS count';
            const params = { privateToken: privateToken };
            const { records } = await tx.run(query, params);
            return records[0].get('count') > 0;
        });

        return result > 0;
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

async function checkIfTheyHaveAnFriendship(privateToken, publicTokenOfMaybeFriend) {
    try {
        const result = await session.readTransaction(async (tx) => {
            const query = 'MATCH (You:User {privateToken: $privateToken})-[a:Friend]->(Friend:User {publicToken: $publicToken})' +
                'RETURN count(a) AS count';
            const params = { privateToken: privateToken, publicToken: publicTokenOfMaybeFriend };
            const { records } = await tx.run(query, params);
            return records[0].get('count') > 0;
        });
        return result > 0;
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

async function getPublicTokenForPrivateToken(privateToken) {
    try {
        return await session.readTransaction(async (tx) => {
            const query = 'MATCH (n:User {privateToken: $privateToken}) RETURN n.publicToken';
            const params = {privateToken: privateToken};
            const {records} = await tx.run(query, params);
            return records[0].get('n.publicToken');
        });
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

async function getUserNameForPrivateToken(privateToken) {
    try {
        return await session.readTransaction(async (tx) => {
            const query = 'MATCH (n:User {privateToken: $privateToken}) RETURN n.name';
            const params = {privateToken: privateToken};
            const {records} = await tx.run(query, params);
            return records[0].get('n.name');
        });
    } catch (error) {
        console.error(error);
        throw new Error("20");
    }
}

async function validatePrivateToken(privateToken) {
    return (privateToken.length === privateTokenLength
        && containsOnlyCharset(privateToken, tokenCharacters)
        && (await checkIfPrivateTokenExists(privateToken)));
}

async function validatePublicToken(publicToken) {
    return (publicToken.length === publicTokenLength
        && containsOnlyCharset(publicToken, tokenCharacters)
        && (await checkIfPublicTokenExists(publicToken)));
}

function containsOnlyCharset(str, charset) {
    for (let i = 0; i < str.length; i++) {
        if (!charset.includes(str[i])) {
            return false;
        }
    }
    return true;
}

function generateRandomString(length) {
    let randomString = '';

    for (let i = 0; i < length; i++) {
        const randomIndex = Math.floor(Math.random() * tokenCharacters.length);
        randomString += tokenCharacters.charAt(randomIndex);
    }

    return randomString;
}

async function generatePublicToken() {
    let token = generateRandomString(publicTokenLength);

    while (await checkIfPublicTokenExists(token) === true) {
        token = generateRandomString(publicTokenLength);
    }

    return token;
}

async function generatePrivateToken() {
    let token = generateRandomString(privateTokenLength);

    while (await checkIfPrivateTokenExists(token) === true) {
        token = generateRandomString(privateTokenLength);
    }

    return token;
}

module.exports = {
    db_addUser: addUser,
    db_addFriend: addFriend,
    db_changeUserName: changeUserName,
    db_removeFriend: removeFriend,
    db_getFriends: getFriends
};